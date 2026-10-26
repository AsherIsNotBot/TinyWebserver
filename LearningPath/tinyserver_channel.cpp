#include<iostream>
#include<vector>
#include<cstring>
#include<string>
#include<unistd.h>
#include<fcntl.h>
#include<functional>
#include<unordered_map>
#include<unordered_set>

#include<sys/socket.h>
#include<sys/epoll.h>
#include<errno.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<memory>

using namespace std;

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
    function<void(int)> remove_callback; // 这个回调函数具体实现在哪

    // 处理 peddingRemove
    void handle_peddingRemove(){
        for(int fd : peddingRemove){
            auto it=channels.find(fd);
            if(it==channels.end())
            continue;

            // 从epoll中删除fd
            if(epoll_ctl(epfd,EPOLL_CTL_DEL,fd,nullptr)<0){
                perror("epoll_ctl delete error");
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
            perror("epoll_create1 error");
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
            perror("epoll_ctl add error");
            exit(1);
        }
        channels[fd]=move(channel);
    }

    void update_channel(Channel* channel){
        epoll_event event{};
        event.data.ptr=channel;
        event.events=channel->getevent();
        if(epoll_ctl(epfd,EPOLL_CTL_MOD,channel->getfd(),&event)<0){
            perror("update_channel error");
            exit(1);
        }
    }

    void loop(){
        while(1){
            int n=epoll_wait(epfd,events.data(),static_cast<int>(events.size()),-1);
            if(n<0){
                if(errno==EINTR)
                continue;
                perror("epoll_wait error");
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
    string outBuffer;
    bool closed;
public:
    Connection(Eventloop& newloop,int newclientfd):loop(newloop),
    clientfd(newclientfd),closed(false){
        ptrChannel=make_unique<Channel>(newclientfd,EPOLLIN);
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
        cout<<"Client closed , fd = "<<clientfd<<endl;
        // close(clientfd);
        closed=true;
        // 逻辑上标注remove
        channel->disable();
        // 加入peddingremove
        loop.remove_channel(clientfd);
    }

    void handleWrite(){
        while(!outBuffer.empty()){
            ssize_t n=send(clientfd,outBuffer.data(),outBuffer.size(),
            // 防止 对端关闭 导致 SIGPIPE
            MSG_NOSIGNAL);

            if(n>0){
                cout<<"send "<<n<<" bytes to fd "<<clientfd<<endl;
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

            perror("send error");
            handleClose();
            return ;
        }

        // outBuffer 发送完毕
        // 关闭 EPOLLOUT。
        // 因为 socket 大多数时候一直处于可写状态，
        // 如果一直监听 EPOLLOUT，
        // epoll 会不断返回 EPOLLOUT。
        // 这样 CPU 会不断空转。
        channel->setevent(EPOLLIN);
        loop.update_channel(channel);
    }

    void handleRead(){
        char buf[1024];
        while(1){
            ssize_t n=recv(clientfd,buf,sizeof(buf),0);
            if(n>0){
                cout<<"recv "<<n<<" bytes from "<<clientfd<<endl;
                string temp(buf,n);
                cout<<"data : "<<temp<<endl;

                outBuffer.append(buf,n);

                // 准备发送数据
                // 修改监听状态
                channel->setevent(EPOLLIN | EPOLLOUT);
                loop.update_channel(channel);

                // 持续读取数据，直到读完
                continue;
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

                perror("recv error");
                handleClose();
                return ;
            }
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
    }
};

void setnonblock(int fd){
    int flags=fcntl(fd,F_GETFL,0);
    if(flags<0){
        perror("fcntl getfile error");
        exit(1);
    }
    if(fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){
        perror("fcntl setfile error");
        exit(1);
    }
}



class TcpServer{
private:
    Eventloop loop;
    int listenfd;
    unordered_map<int,shared_ptr<Connection> > connecions;

public:
    TcpServer(int port):loop(1024),listenfd(-1){
        listenfd=socket(AF_INET,SOCK_STREAM,0);
        if(listenfd<0){
            perror("socket error");
            exit(1);
        }
        int opt=1;
        if(setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))<0){
            perror("setsockopt error");
            exit(1);
        }

        sockaddr_in addr{};
        addr.sin_family=AF_INET;
        addr.sin_port=htons(port);
        addr.sin_addr.s_addr=htonl(INADDR_ANY);
        if(bind(listenfd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0){
            perror("bind error");
            exit(1);
        }

        if(listen(listenfd,128)<0){
            perror("listen error");
            exit(1);
        }
        setnonblock(listenfd);
        // 设置
        loop.set_remove_callback([this](int fd){
            removeConnection(fd);
        });
        cout<<"Server start listen on port "<<port<<endl;
    }
    ~TcpServer(){
        if(listenfd>=0)
        close(listenfd);
    }

    // 启动服务器
    void start(){
        auto listenChannel=make_unique<Channel>(listenfd,EPOLLIN);
        listenChannel->set_read_callback([this]{
            handleListen();
        });
        loop.add_channel(move(listenChannel));
        loop.loop();
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
                perror("accept error");
                break;
            }
            setnonblock(clientfd);

            cout<<"New Client connected, fd = "<<clientfd<<endl;
            // 创建客户端，把剩下的事交给 createClient 处理
            createClient(clientfd);
        }
    }
    void createClient(int clientfd){
        auto connection=make_shared<Connection>(loop,clientfd);
        connecions[clientfd]=connection;
        connection->start();
    }
    void removeConnection(int clientfd){
        auto it=connecions.find(clientfd);
        if(it==connecions.end())
        return ;
        cout<<"Remove connection fd = "<<clientfd<<endl;
        connecions.erase(it);
    }
};

int main(){
    cout<<"Echo sever start !"<<endl;
    // 启动 TcpServer
    TcpServer server(8080);
    server.start();
}