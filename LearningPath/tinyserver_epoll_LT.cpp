#include<iostream>
#include<unistd.h>
#include<cstring>
#include<fcntl.h>

#include<sys/epoll.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<errno.h>
using namespace std;

void setnonblock(const int& fd){
    int flags=fcntl(fd,F_GETFL,0);
    if(flags<0){
        perror("fcntl error");
        exit(1);
    }
    if(fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){
        perror("fcntl error");
        exit(1);
    }
}

int main(){

    cout<<"Echo sever 启动!"<<endl;
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0){
        perror("socket error");
        return 1;
    }

    setnonblock(fd);

    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(8080);
    addr.sin_addr.s_addr=INADDR_ANY; // 127.0.0.1

    // 绑定端口和ip
    if(bind(fd,(sockaddr*)&addr,sizeof(addr))<0){
        perror("bind error");
        return 1;
    }

    if(listen(fd,10)<0){
        perror("listen error");
        return 1;
    }
    cout<<"正在监听8080端口！"<<'\n';

    // 创建epoll实例
    int epfd=epoll_create1(0);
    if(epfd<0){
        perror("epoll_create error");
        return 1;
    }

    // 把监听的socket加入epoll
    epoll_event event{};
    event.data.fd=fd;
    event.events=EPOLLIN;

    if(epoll_ctl(epfd,EPOLL_CTL_ADD,fd,&event)<0){
        perror("error_ctl error");
        return 1;
    }

    // 存放 epoll 返回事件
    epoll_event events[1024];

    while(1){

        int ret=epoll_wait(epfd,events,1024,-1);
        if(ret<0){
            if(errno==EINTR)
            continue;
            perror("epoll_wait error");
            break;
        }

        // 处理所有准备好的fd
        for(int i=0;i<ret;i++){

            int curfd=events[i].data.fd;
            // 如果 curfd 是监听fd
            if(curfd==fd){
                int clientfd=accept(fd,nullptr,nullptr);
                if(clientfd<0){
                    if(errno==EAGAIN || errno==EWOULDBLOCK)
                    continue;
                    if(errno==EINTR)
                    continue;
                    perror("accept error");
                    continue;
                }
                setnonblock(clientfd);
                cout<<"新客户端连接 fd="<<clientfd<<endl;

                epoll_event clientevent{};
                clientevent.data.fd=clientfd;
                clientevent.events=EPOLLIN;
                
                if(epoll_ctl(epfd,EPOLL_CTL_ADD,clientfd,&clientevent)<0){
                    perror("event epoll_ctl error");
                    close(clientfd);
                    return 1;
                }

            }
            // 如果 curfd 不是监听fd，说明客户端有数据
            else{

                // LT 版本
                char buf[4];
                memset(buf,0,sizeof(buf));
                int len=recv(curfd,buf,sizeof(buf),0);

                if(len==0){
                    cout<<"客户端 主动关闭连接 fd="<<curfd<<endl;

                    // 删除 curfd
                    if(epoll_ctl(epfd,EPOLL_CTL_DEL,curfd,nullptr)<0){
                        perror("epoll_ctl error");
                    }
                    close(curfd);
                }
                // recv 读入出错
                else if(len<0){

                    if(errno==EAGAIN || errno==EWOULDBLOCK)
                    continue;
                    if(errno==EINTR)
                    continue;

                    perror("recv  error");
                    // 删除 curfd
                    if(epoll_ctl(epfd,EPOLL_CTL_DEL,curfd,nullptr)<0){
                        perror("epoll_ctl error");
                    }
                    close(curfd);
                }
                // 收到数据
                else{
                    cout<<"受到客户端数据 fd = "<<curfd<<" 共 "<<len<<"字节"<<endl;
                    cout.write(buf,len);
                    cout<<endl;
                    // Echo 发回给客户端
                    send(curfd,buf,len,0);
                }

            }
        }
    }
    close(epfd);
    close(fd);
}