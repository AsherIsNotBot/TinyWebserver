#include<iostream>
#include<cstring>
#include<string>
#include<unistd.h>

#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
using namespace std;

int main(int argc,char* argv[]){

    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0){
        perror("socket error");
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(8080);
    addr.sin_addr.s_addr=INADDR_ANY;

    int ret=bind(fd,(sockaddr*)&addr,sizeof(addr));
    if(ret<0){
        perror("bind error");
        return 1;
    }

    ret=listen(fd,10);
    if(ret<0){
        perror("listen error");
        return 1;
    }

    cout<<"程序开始监听8080端口"<<endl;
    while(1){
        cout<<"等待客户端连接..."<<endl;
        int connfd=accept(fd,nullptr,nullptr);
        if(connfd<0){
            perror("accept error");
            continue;
        }
        cout<<"客户端连接成功！"<<endl;

        char buf[1024];
        while(1){
            memset(buf,0,sizeof(buf));
            int n=recv(connfd,buf,sizeof(buf)-1,0);
            if(n>0){
                cout<<"接受信息:"<<endl;
                cout<<buf<<endl;
            }
            else if(n==0){
                cout<<"服务器断开连接!"<<endl;
                break;
            }
            else{
                perror("recv error");
                break;
            }
        }
        close(connfd);
    }
    close(fd);
    cout<<"监听关闭"<<endl;
}