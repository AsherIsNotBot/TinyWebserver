#include<iostream>
#include<string>
#include<cstring>
#include<vector>
#include<unordered_map>
#include<unistd.h>
#include<fcntl.h>
#include<functional>

#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<errno.h>
using namespace std;

// Reactor 架构
// EventLoop
class Eventloop{
private:
    int epfd;
    vector<epoll_event> events;
    // fd -> 对应的事件处理函数
    unordered_map<int,function<void(uint32_t)> > callbacks; // 为什么这个function的参数只能是void(uint32_t)

public:
    Eventloop(int MaxLength=1024):events(MaxLength){
        epfd=epoll_create1(0);
        if(epfd<0){
            perror("epoll_create1 error");
            exit(1);
        }
    }
    ~Eventloop(){
        close(epfd);
    }

    // 注册fd
    void add(int fd,uint32_t ev,function<void(uint32_t)> cb){
        epoll_event event{};
        event.data.fd=fd;
        event.events=ev;
        if(epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&event)<0){
            perror("epoll_ctl add error");
            exit(1);
        }
        // 使用move转移回调对象的所有权
        callbacks[fd]=move(cb);
    }

    // 删除fd
    void remove(int fd){
        if(epoll_ctl(epfd,EPOLL_CTL_DEL,fd,nullptr)<0){
            perror("epoll_ctl delete error");
        }
        callbacks.erase(fd);
    }

    void loop(){
        while(1){
            int n=epoll_wait(epfd,events.data(),events.size(),-1);
            if(n<0){
                if(errno==EINTR)
                continue;
                perror("epoll_wait error");
                break;
            }
            
            for(int i=0;i<n;i++){
                int fd=events[i].data.fd;
                uint32_t cv=events[i].events;
                auto it=callbacks.find(fd);
                if(it!=callbacks.end()){
                    // 调用 fd 所对应的函数
                    it->second(cv);
                    
                }
            }

        }
    }
};

void setnonblock(const int fd){
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

int main(){

    cout << "Echo Server 启动!" << endl;

    int listenfd=socket(AF_INET,SOCK_STREAM,0);
    if(listenfd<0){
        perror("socket error");
        return 1;
    }

    int opt=1; // 允许本地端口短时间被重复绑定/复用
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(8080);
    addr.sin_addr.s_addr=INADDR_ANY;

    if(bind(listenfd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0){
        perror("bind error");
        return 1;
    }

    if(listen(listenfd,10)<0){
        perror("listen error");
        return 1;
    }
    setnonblock(listenfd);

    // 创建 EventLoop 实例
    Eventloop loop;

    // 注册监听 fd
    loop.add(listenfd,EPOLLIN,[&](uint32_t){
        while(1){
            sockaddr_in addr{};
            socklen_t len=sizeof(addr);
            int clientfd=accept(listenfd,reinterpret_cast<sockaddr*>(&addr),&len);
            if(clientfd<0){
                // 为什么这里是break，这里的errno到底是什么意思
                if(errno==EAGAIN || errno==EWOULDBLOCK)
                break;
                if(errno==EINTR)
                continue;
                perror("accept error");
                break;
            }
            setnonblock(clientfd);
            cout<<"新客户端连接 : fd = "<<clientfd<<endl;

            // 把客户端 fd 注册到 Reactor
            loop.add(clientfd,EPOLLIN,[&,clientfd](uint32_t ev){
                char buf[1024];
                int n=recv(clientfd,buf,sizeof(buf),0);
                if(n>0){
                    cout<<"收到数据 : "<<'\n';
                    string temp(buf,n);
                    cout<<temp<<endl;
                    if(send(clientfd,buf,n,0)<0){
                        perror("send error");
                    }
                }
                else if(n==0){
                    cout<<"客户端断开连接 "<<clientfd<<'\n';
                    loop.remove(clientfd);
                    close(clientfd);
                }
                else{
                    if(errno==EAGAIN || errno==EWOULDBLOCK)
                    return ;
                    if(errno==EINTR)
                    return ;
                    perror("recv error");
                    loop.remove(clientfd);
                    close(clientfd);
                }
            });
        }
    });

    // 启动 Reactor 
    loop.loop();
    close(listenfd);

}