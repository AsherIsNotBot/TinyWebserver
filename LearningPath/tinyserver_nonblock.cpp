#include<iostream>
#include<cstring>
#include<string>
#include<unistd.h>
#include<fcntl.h>
#include<errno.h>

#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
using namespace std;

void setnonblock(int fd){

    int flag=fcntl(fd,F_GETFL,0);
    
    if(flag<0){
        perror("fcntl error");
        exit(1);
    }
    if(fcntl(fd,F_SETFL,flag|O_NONBLOCK)<0){
        perror("fcntl error");
        exit(1);
    }
}


int main(int argc,char* argv[]){

    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0){
        perror("socket error");
        return 1;
    }

    setnonblock(fd);

    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(8080);
    addr.sin_addr.s_addr=INADDR_ANY;

    if(bind(fd,(sockaddr*)&addr,sizeof(addr))<0){
        perror("bind error");
        return 1;
    }

    if(listen(fd,10)<0){
        perror("listen error");
        return 1;
    }

    cout<<"程序开始监听8080端口"<<endl;
    while(1){
        cout<<"等待客户端连接..."<<endl;
        int connfd=accept(fd,nullptr,nullptr);

        if(connfd>=0){

            setnonblock(connfd);
            cout<<"客户端连接成功！"<<endl;
            char buf[1024];

            while(1){
                memset(buf,0,sizeof(buf));
                // int n=recv(connfd,buf,sizeof(buf)-1,0);
                int ret=read(connfd,buf,sizeof(buf)); // 这里为什么不用recv
                
                if(ret>0){
                    cout<<"接受信息:"<<endl;
                    cout.write(buf,ret); // 这里的write有什么作用
                    cout<<endl;
                    write(connfd,buf,ret);
                }
                else if(ret==0){
                    cout<<"服务器断开连接!"<<endl;
                    break;
                }
                else{
                    if(errno==EAGAIN || errno==EWOULDBLOCK){
                        // 无数据
                        continue;
                    }
                    else if(errno==EINTR){ // 这里是在判断什么
                        continue;
                    }
                    perror("read error");
                    break;
                }
            }
            close(connfd);
        }
        else{
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK) {
                // 当前没有新连接
            }
            else if (errno == EINTR) {
                continue;
            }
            else {
                perror("accept");
            }
        }
        sleep(2);
        
    }
    close(fd);
    cout<<"监听关闭"<<endl;
}