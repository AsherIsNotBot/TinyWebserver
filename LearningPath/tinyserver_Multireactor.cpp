#include <thread>
#include <atomic>
#include <mutex>
#include<condition_variable>
#include <csignal>
#include <cstdlib>

#include<iostream>
#include<vector>
#include<queue>
#include<string>
#include<unistd.h>
#include<fcntl.h>
#include<functional>
#include<unordered_map>
#include<unordered_set>
#include<algorithm>
#include<sstream>
#include <fstream>
#include <chrono> 

#include<sys/socket.h>
#include<sys/epoll.h>
#include<errno.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<memory>

using namespace std;

namespace {
    string toLower(string s){
        transform(s.begin(),s.end(),s.begin(),[](unsigned char c){
            return tolower(c);
        });
        return s;
    }

    // 去除字符串首尾的空格与指表符
    string thrim(const string&  s){ // 为什么不能把下面的两个相同的函数拿来用
        size_t b=s.find_first_not_of(" \t"); 
        if(b==string::npos) 
        return "";
        size_t e=s.find_last_not_of(" \t");
        return s.substr(b,e-b+1);
    }
    bool readFile(const string& path,string& out){ 
        ifstream ifs(path,ios::binary); // 以二进制模式打开文件
        if(!ifs)
        return false;
        ostringstream oss;
        oss<<ifs.rdbuf(); // 获取文件底层原始数据缓冲区，输入字符串流中
        out=oss.str();
        return true;
    }
    string mimeType(const string& path){
        size_t dot=path.find_last_of('.');
        if(dot==string::npos)
        return "application/octet-stream";
        string ext=path.substr(dot+1);
        ext=toLower(ext);

        if(ext=="html" || ext=="htm")
        return "text/html; charset=utf-8";
        if(ext=="css")
        return "text/css";
        if(ext=="js")
        return "application/javascript";
        if(ext=="json")
        return "application/json";
        if(ext=="png")
        return "image/png";
        if(ext=="gif")
        return "image/gif";
        if(ext=="txt")
        return "text/plain; charset=utf-8";
        return "application/octet-stream";
    }
}

// 存储解析后的 http 请求信息
struct HttpRequest{
    string method; // 请求方法
    string url; // 请求地址
    string version; // http协议版本
    unordered_map<string,string> headers; // 存储不同首部字段名
    string body; // 请求数据
    bool keepAlive=false; // 长连接
};

class HttpParser{
public:
    // 解析http报文
    static int parse(const string& data,size_t& consumed,HttpRequest& req){
        consumed=0;
        // 查找 "\r\n\r\n" ,这是http头部和请求体的分界线
        size_t headerEnd=data.find("\r\n\r\n");

        // 如果找不到，说明客户端的数据还没有发完，暂时不解析
        if(headerEnd==string::npos)
        return 0;
        
        size_t bodyStart=headerEnd+4; // 请求体在空行后开始
        size_t lineEnd=data.find("\r\n");

        // 防止格式错误
        if(lineEnd==string::npos)
        return -1;

        string requestLine=data.substr(0,lineEnd);

        //  读取方法，URL，版本
        istringstream iss(requestLine);
        if(!(iss>>req.method>>req.url>>req.version)){
            return -1;
        }

        // 检验版本号
        if(req.version.rfind("HTTP/",0)!=0) 
        return -1;

        size_t cur=lineEnd+2; // 来到第一行的首部
        while(cur<headerEnd){
            size_t e=data.find("\r\n",cur);
            if(e==string::npos || e>headerEnd)
            break;

            string line=data.substr(cur,e-cur);
            size_t colon=line.find(':');

            // 拆分 Key 和 Value
            if(colon!=string::npos){
                // Key 统一转成 小写并去除空格
                string key=toLower(thrim(line.substr(0,colon)));
                // Value 只需去除空格
                string val=thrim(line.substr(colon+1));
                if(!key.empty()){
                    req.headers[key]=val;
                }
            }
            cur=e+2; // 移动到下一行
        }

        // 处理请求体
        size_t contentLength=0;
        auto it=req.headers.find("content-length");
        if(it!=req.headers.end()){
            try{
                contentLength=stoul(it->second);
            } catch(...){
                return -1;
            }
        }
        // 检查缓冲区收到的数据是否已经包含完整的Body
        if(data.size()<bodyStart+contentLength){
            return 0;
        }
        // 提取body
        req.body=data.substr(bodyStart,contentLength);

        // 记录成功解析花费缓冲区多少字节
        consumed=bodyStart + contentLength;

        // 判断是否是长连接
        // HTTP/1.1 默认是长连接
        // HTTP/1.0 默认是短连接
        string conn;
        auto connIt=req.headers.find("connection");
        if(connIt!=req.headers.end()){
            conn=connIt->second;
        }
        if(req.version=="HTTP/1.1"){
            req.keepAlive=(conn!="close");
        }
        else{
            req.keepAlive=(conn=="keep-alive");
        }
        return 1;
    }
};

#define LOG_INFO(msg) do { ostringstream oss; oss << msg; Log::instance().log("INFO ", oss.str()); } while(0)
#define LOG_ERROR(msg) do { ostringstream oss; oss << msg; Log::instance().log("ERROR", oss.str()); } while(0)

class Log{
private:
    queue<string> queue_; // 阻塞队列，缓存日志
    mutex mtx; // 互斥锁
    condition_variable cv; // 条件变量，队列为空时让后台线程睡觉，有数据时唤醒
    thread thread_; //后台写日志的线程
    atomic<bool> running_{false}; // 记录 日志系统是否还在运行
    ofstream ofs; // 文件输出流
    
    Log(){}; // 单例模式，私有化构造函数

    // 后台工作的死循环
    void work(){ 
        while(true){
            string line;
            {
                unique_lock<mutex> lock(mtx);
                cv.wait(lock,[this](){ 
                    // 如果队列为空 且还在运行，就睡觉等待唤醒
                    return !queue_.empty() ||!running_.load();
                });
                if(queue_.empty()&&(!running_.load()))
                break;
                line=move(queue_.front());
                queue_.pop();

            }
            // 写入文件
            if(ofs.is_open()){ 
                ofs<<line;
                ofs.flush();
            }
            else{
                cout<<line;
            }
        }
    }
public:
    ~Log(){};
    static Log& instance(){
        static Log inst;
        return inst;
    }
    // 初始化，打开日志文件并启动后台线程
    void init(const string& filename){
        if(running_.load())
        return ;
        ofs.open(filename,ios::out|ios::app); 
        running_.store(true);
        thread_=thread(&Log::work,this); 
    }

    // 停止日志系统，等待残留的日志写完
    void stop(){
        if(!running_.exchange(false)) 
        return ;
        cv.notify_all();
        if(thread_.joinable())
        thread_.join();
        if(ofs.is_open())
        ofs.close();
    }

    // 前端线程调用这个接口提交日志
    void log(const string& level,const string& msg){
        // 获取时间
        time_t t=time(nullptr); // 时间戳，从1970到现在过了多少秒
        struct tm*tm_info=localtime(&t); // 把时间戳转换成 年月日时分秒 的结构体
        char timebuf[32]; 
        strftime(timebuf,sizeof(timebuf),"%Y-%m-%d %H:%M:%S",tm_info); // 从结构体格式化成字符串
        string line=string(timebuf)+"["+level+"]"+msg+'\n';
        {
            lock_guard<mutex> lock(mtx);
            queue_.push(move(line));
        }
        cv.notify_one(); // 唤醒线程
    }
};

using Clock = chrono::steady_clock; // 单调时钟
using TimeStamp = Clock::time_point;
using MS = chrono::milliseconds;

struct TimerNode{
    int fd;
    TimeStamp expires; // 过期绝对时间戳
    function<void()> cb;  // 超时回调函数
    bool operator<(const TimerNode& t) const {
        return expires < t.expires;
    }
};

struct HeapTimer {
private:
    vector<TimerNode> heap_;
    unordered_map<int,size_t> ref_; // fd->heap_数组中的下标
    mutex mtx_;
    void swapNode(size_t i,size_t j){
        swap(heap_[j],heap_[i]);
        ref_[heap_[i].fd]=i;
        ref_[heap_[j].fd]=j; 
    }
    void siftup(size_t i){
        while(i>0){
            size_t parent=(i-1)/2;
            if(heap_[i]<heap_[parent]){
                swapNode(i,parent);
                i=parent;
            }
            else
            break;
        }
    }
    bool siftdown(size_t index,size_t n){
        size_t i=index;
        size_t child=i*2+1;
        while(child<n){
            if(child+1<n && heap_[child+1]<heap_[child]){
                child++;
            }
            if(heap_[child]<heap_[i]){
                swapNode(i,child);
                i=child;
                child=i*2+1;
            }
            else
            break;
        }
        return i>index;
    }
    void del_(size_t index){
        size_t n=heap_.size()-1;
        if(index<n){
            swapNode(index,n);
            if(!siftdown(index,n))
            siftup(index);
        }
        ref_.erase(heap_.back().fd);
        heap_.pop_back();
    }

public:
    // 添加定时器
    void add(int fd,int timeout_ms,function<void()> cb){
        lock_guard<mutex> lock(mtx_);
        TimeStamp new_expires = Clock::now()+MS(timeout_ms);
        if(ref_.count(fd)==0){
            // 插入堆尾
            size_t i =heap_.size();
            ref_[fd]=i;
            heap_.push_back({fd,new_expires,move(cb)});
            siftup(i);
        }
        else{
            // 已有连接：原地更新过期时间
            size_t i =ref_[fd];
            heap_[i].expires=new_expires;
            if(cb)
            heap_[i].cb=move(cb);
            if(!siftdown(i,heap_.size())){
                siftup(i);
            }
        }
    }
    void remove_fd(int fd){
        lock_guard<mutex> lock(mtx_);
        auto it =ref_.find(fd);
        if(it!=ref_.end())
        del_(it->second);
    }
    int getNextTick(){ // 检查并剔除所有超时连接，返回下一个最快过期时间连接
        vector<function<void()> > expired_cbs;
        int next_ms=-1;
        {
            lock_guard<mutex> lock(mtx_);
            TimeStamp now = Clock::now();
            while(!heap_.empty()){
                const TimerNode& node =heap_.front();
                if(chrono::duration_cast<MS>(node.expires-now).count()>0)
                break;
                expired_cbs.push_back(node.cb);
                del_(0);
            }
            if(!heap_.empty()){
                auto diff=chrono::duration_cast<MS>(heap_.front().expires-Clock::now()).count();
                next_ms=(diff>0) ? static_cast<int> (diff) : 0;
            }
        }
        for(auto& cb : expired_cbs){
            if(cb)
            cb();
        }
        return next_ms;
    }
};

// channel 类 (根据事件调用相关 call_back)

class Channel{
private:
    int fd;
    uint32_t event;
    bool active;
    function<void()> read_callback;
    function<void()> write_callback;
    function<void()> close_callback;

public:
    Channel(int newfd):fd(newfd),event(0),active(true){}
    Channel(int newfd,uint32_t newevent):fd(newfd),event(newevent),active(true){}

    int getfd(){
        return fd;
    }
    uint32_t getevent(){
        return event;
    }
    void setevent(uint32_t newevent){
        event=newevent;
    }
    void disable(){
        active=false;
        event=0;
    }
    void set_read_callback(function<void()> cb){
        read_callback=move(cb);
    }
    void set_write_callback(function<void()> cb){
        write_callback=move(cb);
    }
    void set_close_callback(function<void()> cb){
        close_callback=move(cb);
    }
    void handle_event(uint32_t ev){ 
        if(!active){
            // 如果已经请求删除删除，不执行事件
            return ;
        }
        if((ev & EPOLLIN) && read_callback){
            read_callback();
            if(!active){
                // 如果在 read_callback 里调用close
                return ;
            }
        }
        if((ev & EPOLLOUT) && write_callback){
            write_callback();
            if(!active){
                // 如果在 read_callback 里调用close
                return ;
            }
        }
        if((ev & (EPOLLHUP | EPOLLERR)) && close_callback){
            close_callback();
        }
    }
};

class Eventloop{
private:
    int epfd;
    vector<epoll_event> events;
    unordered_map<int,unique_ptr<Channel> > channels;
    unordered_set<int> peddingRemove;
    function<void(int)> remove_callback; 
    HeapTimer timer_; // 小根据堆实例

    // 处理 peddingRemove
    void handle_peddingRemove(){
        for(int fd : peddingRemove){
            auto it=channels.find(fd);
            if(it==channels.end())
            continue;

            timer_.remove_fd(fd); // 从定时器堆中删除

            // 从epoll中删除fd
            if(epoll_ctl(epfd,EPOLL_CTL_DEL,fd,nullptr)<0){
                LOG_ERROR("epoll_ctl delete error");
                exit(1);
            }
            // 从EventLoop中删除fd
            channels.erase(fd);
            // 通知TcpServer，fd已经安全删除
            if(remove_callback){
                remove_callback(fd);
            }
            
        }
        peddingRemove.clear();
    }
public:
    Eventloop(int maxlen):events(maxlen){
        epfd=epoll_create1(0);
        if(epfd<0){
            LOG_ERROR("epoll_create1 error");
            exit(1);
        }
    };
    ~Eventloop(){
        close(epfd);
    }
    void set_remove_callback(function<void(int)> cb){
        remove_callback=move(cb);
    }
    void remove_channel(int fd){
        peddingRemove.insert(fd);
    }
    void add_channel(unique_ptr<Channel> channel){
        Channel* ptr=channel.get();

        int fd=channel->getfd();
        epoll_event event{};
        event.data.ptr=ptr; // 这里先get，让event拿到channel的真实地址，再move转移所有权，防止ptr失效
        event.events=channel->getevent();

        if(epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&event)<0){
            LOG_ERROR("epoll_ctl add error");
            exit(1);
        }
        channels[fd]=move(channel);
    }

    void update_channel(Channel* channel){
        epoll_event event{};
        event.data.ptr=channel;
        event.events=channel->getevent();
        if(epoll_ctl(epfd,EPOLL_CTL_MOD,channel->getfd(),&event)<0){
            LOG_ERROR("update_channel error");
            exit(1);
        }
    }

    void refresh_timer(int fd,int timeout_ms,function<void()> cb){
        timer_.add(fd,timeout_ms,move(cb)); // 供 Connection 调用的定时器刷新接口
    }

    void loop(){
        int time_ms=-1;
        while(1){
            // 限制单次最大阻塞为 1000 ms ，确保新分配的静默连接可以及时纳入超时巡检
            int wait_ms =(time_ms <0 || time_ms >1000) ? 1000 : time_ms;
            int n=epoll_wait(epfd,events.data(),static_cast<int>(events.size()),wait_ms);
            if(n<0){
                if(errno==EINTR)
                continue;
                LOG_ERROR("epoll_wait error");
                break;
            }
            for(int i=0;i<n;i++){
                Channel* channel=static_cast<Channel*> (events[i].data.ptr);
                int fd=channel->getfd();
                // 如果fd已经存在 peddingRemove中跳过
                if(peddingRemove.count(fd))
                continue;
                channel->handle_event(events[i].events);

            }
            // 巡检小根堆，将所有僵尸连接推入 peddingRemove
            time_ms = timer_.getNextTick();
            handle_peddingRemove();
        }
    }
};
// 管理连接
class Connection{
private:
    Eventloop& loop;
    int clientfd;
    unique_ptr<Channel> ptrChannel;
    Channel* channel;
    string inBuffer; // 读缓冲区
    string outBuffer;
    bool closed;
    bool keepAlive;
    static const int TIMEOUT_MS=15000; // 超时时间限制
    void extendLife(){
        if(closed)
        return ;
        loop.refresh_timer(clientfd,TIMEOUT_MS,[this]{
            LOG_INFO("Connecion timeout , kicking zombie client fd = "+to_string(clientfd));
            handleClose();
        });
    }
public:
    Connection(Eventloop& newloop,int newclientfd):loop(newloop),
    clientfd(newclientfd),closed(false),keepAlive(false){
        ptrChannel=make_unique<Channel>(newclientfd,EPOLLIN | EPOLLET); 
        channel=ptrChannel.get();
        closed=false;
    }
    ~Connection(){
        if(clientfd>=0){
            close(clientfd);
            clientfd=-1;
        }
    }
    void handleClose(){
        if(closed) 
        return ;
        LOG_INFO("Client closed , fd = "+to_string(clientfd));
        // close(clientfd);
        closed=true;
        // 逻辑上标注remove
        channel->disable();
        // 加入peddingremove
        loop.remove_channel(clientfd);
    }

    void handleWrite(){
        extendLife();  // 更新倒计时
        while(!outBuffer.empty()){
            ssize_t n=send(clientfd,outBuffer.data(),outBuffer.size(),
            // 防止 对端关闭 导致 SIGPIPE
            MSG_NOSIGNAL);

            if(n>0){
                LOG_INFO("send "+to_string(n)+" bytes to fd "+to_string(clientfd));
                // 删除已发的数据
                outBuffer.erase(0,n);
                continue;
            }
            if(n<0){
                if(errno==EINTR)
                continue;
                if(errno==EAGAIN || errno==EWOULDBLOCK)
                return ;
            }

            LOG_ERROR("send error");
            handleClose();
            return ;
        }
        if(!keepAlive){
            handleClose();
            return ;
        }
        
        // outBuffer 发送完毕
        // 关闭 EPOLLOUT。
        // 因为 socket 大多数时候一直处于可写状态，
        // 如果一直监听 EPOLLOUT，
        // epoll 会不断返回 EPOLLOUT。
        // 这样 CPU 会不断空转。
        channel->setevent(EPOLLIN | EPOLLET);
        loop.update_channel(channel);
    }

    void serverRequst(const HttpRequest& req){
        // http 响应报文的基础部分
        string statusLine;
        string headers;
        string bodyContent;
        
        string root="./www";
        // 只处理get请求
        if(req.method!="GET"){
            statusLine="HTTP/1.1 405 Method Not Allowed\r\n";
            bodyContent="405 Method Not Allowed";
            headers+="content-Type: text/plain; charset=utf-8\r\n";
        }
        else{
            string path=req.url;
            // 去除 URL参数
            size_t q=path.find('?');
            if(q!=string::npos)
            path=path.substr(0,q);

            // 默认访问 index.html
            if(path.empty()||path=="/")
            path="/index.html";

            // 防止目录穿越
            if(path.find("..")!=string::npos){ 
                statusLine="HTTP/1.1 403 Forbidden\r\n";
                bodyContent="403 Forbidden";
                headers+="content-Type: text/plain; charset=utf-8\r\n";
            }
            else{
                string fullPath=root+path;
                if(readFile(fullPath,bodyContent)){
                    statusLine="HTTP/1.1 200 OK\r\n";
                    headers+="Content-Type: "+mimeType(path)+"\r\n";
                }
                else{
                    statusLine="HTTP/1.1 404 NOT Found\r\n";
                    bodyContent="404 Not Found";
                    headers+="content-Type: text/plain; charset=utf-8\r\n";
                }
            }
        }
        // 补充 Content-Length 和 Connection 头
        headers+="Content-Length: "+to_string(bodyContent.size())+"\r\n";
        if(keepAlive)
        headers+="Connection: keep-alive\r\n";
        else
        headers+="Connection: close\r\n";

        outBuffer+=statusLine+headers+"\r\n"+bodyContent;
    }

    void handleRead(){
        extendLife();  // 更新倒计时
        char buf[4096];

        // 将所有数据读入 inBuffer
        while(1){
            ssize_t n=recv(clientfd,buf,sizeof(buf),0);
            if(n>0){
                inBuffer.append(buf,n);
            }
            else if(n==0){
                handleClose();
                return ;   
            }
            else{
                if(errno==EINTR)
                continue;
                if(errno==EAGAIN || errno==EWOULDBLOCK)
                break;

                LOG_ERROR("recv error");
                handleClose();
                return ;
            }
        }
        
        // 解析 inBuffer 中的 HTTP 报文
        while(1){
            size_t consumd=0;
            HttpRequest req;
            int r=HttpParser::parse(inBuffer,consumd,req);
            if(r==0){
                break; // 报文没读完
            }
            if(r<0){
                // 格式错误，返回 404 ，并断开
                outBuffer+="HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                keepAlive=false;
                inBuffer.clear();
                break;
            }
            // 解析成功，移除inBuffer的数据
            inBuffer.erase(0,consumd);
            keepAlive=req.keepAlive;
            serverRequst(req); // 处理请求
            if(!keepAlive) // 非长连接，断开
            break;
        }
        if(!outBuffer.empty()){ // 如果outbuffer 有东西要发，就注册写事件
            channel->setevent(EPOLLIN | EPOLLOUT | EPOLLET);
            loop.update_channel(channel);
        }
    }
    void start(){
        ptrChannel->set_close_callback([this]{
            handleClose();
        });
        ptrChannel->set_read_callback([this]{
            handleRead();
        });
        ptrChannel->set_write_callback([this]{
            handleWrite();
        });
        loop.add_channel(move(ptrChannel));
        extendLife();
    }
};

void setnonblock(int fd){
    int flags=fcntl(fd,F_GETFL,0);
    if(flags<0){
        LOG_ERROR("fcntl getfile error");
        exit(1);
    }
    if(fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){
        LOG_ERROR("fcntl setfile error");
        exit(1);
    }
}



class TcpServer{
private:
    unique_ptr<Eventloop> mainloop; // mainloop 负责接受客户端连接
    vector<unique_ptr<Eventloop>> subloops; // subloops 负责已建立连接的读写
    vector<thread> threads; // 用线程为每个Reactor开辟独立执行空间
    atomic<int> nextloop_{0}; // 原子计数器，保证在多线程环境下++操作不发生数据错乱
    int listenfd;
    unordered_map<int,shared_ptr<Connection> > connecions;
    mutex mtx_; // 互斥锁

public:
    TcpServer(int port,int subReactors =4):listenfd(-1){
        mainloop=make_unique<Eventloop>(1024);
        for(int i=0;i<subReactors;i++){
            subloops.push_back(make_unique<Eventloop>(1024));
        }

        listenfd=socket(AF_INET,SOCK_STREAM,0);
        if(listenfd<0){
            LOG_ERROR("socket error");
            exit(1);
        }
        int opt=1;
        if(setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))<0){
            LOG_ERROR("setsockopt error");
            exit(1);
        }

        sockaddr_in addr{};
        addr.sin_family=AF_INET;
        addr.sin_port=htons(port);
        addr.sin_addr.s_addr=htonl(INADDR_ANY);
        if(bind(listenfd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0){
            LOG_ERROR("bind error");
            exit(1);
        }

        if(listen(listenfd,128)<0){
            LOG_ERROR("listen error");
            exit(1);
        }
        setnonblock(listenfd);
        // 设置
        mainloop->set_remove_callback([this](int fd){
            removeConnection(fd);
        });
        for(auto &subloop : subloops){
           subloop->set_remove_callback([this](int fd){
                removeConnection(fd);
            }); 
        }
        LOG_INFO("Server start listen on port "+to_string(port));
    }

    ~TcpServer(){
        if(listenfd>=0)
        close(listenfd);
        for(auto &t:threads){
            if(t.joinable()){
                t.join();
            }
        }
    }

    // 启动服务器
    void start(){
        auto listenChannel=make_unique<Channel>(listenfd,EPOLLIN | EPOLLET);
        listenChannel->set_read_callback([this]{
            handleListen();
        });

        mainloop->add_channel(move(listenChannel));
        for(size_t i=0;i<subloops.size();i++){
            threads.emplace_back([this,i](){ // 原地构造
                subloops[i]->loop();
            });
        }

        mainloop->loop();
    }

private:
    void handleListen(){
        while(1){
            sockaddr_in addr{};
            socklen_t len=sizeof(addr);
            int clientfd=accept(listenfd,reinterpret_cast<sockaddr*>(&addr),&len);

            if(clientfd<0){
                if(errno==EAGAIN || errno==EWOULDBLOCK)
                break;
                if(errno==EINTR)
                continue;
                LOG_ERROR("accept error");
                break;
            }
            setnonblock(clientfd);
            LOG_INFO("New Client connected, fd = "+to_string(clientfd));
            // 创建客户端，把剩下的事交给 createClient 处理
            createClient(clientfd);
        }
    }
    void createClient(int clientfd){
        Eventloop* loop=subloops[nextloop_.fetch_add(1)%subloops.size()].get();
        auto connection=make_shared<Connection>(*loop,clientfd);
        {
            lock_guard<mutex> lock(mtx_);  
            connecions[clientfd]=connection;
        }
        connection->start();
    }
    void removeConnection(int clientfd){
        lock_guard<mutex> lock(mtx_);
        auto it=connecions.find(clientfd);
        if(it==connecions.end())
        return ;
        LOG_INFO("Remove connection fd = "+to_string(clientfd));
        connecions.erase(it);
    }
};



int main(){
    
    // 忽略 SIGPIPE 信号，防止客户端异常断开导致服务器崩溃
    signal(SIGPIPE,SIG_IGN);

    // 初始化单例模式的日志系统，日志路径为当前目录下的 server.log
    Log::instance().init("server.log");
    LOG_INFO("High Performance Web Server start !");

    // 启动 TcpServer
    TcpServer server(8080);
    server.start();

    Log::instance().stop();
}