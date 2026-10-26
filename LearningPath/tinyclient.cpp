#include<iostream>
#include<cstring>
#include<string>
#include<unistd.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>

using namespace std;

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

int main(int argc,char* argv[]){
    int lsfd=socket(AF_INET,SOCK_STREAM,0);
    if(lsfd<0){
        perror("socket error");
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(8080);
    inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
    
    if(connect(lsfd,(sockaddr*)&addr,sizeof(addr))<0){
        perror("connect error");
        return 1;
    }
    cout<<"连接成功！"<<endl;
    string msg;
    while(1){
        cout<<"输入消息：(输入 quit 退出)"<<endl;
        getline(cin,msg);
        if(msg=="quit"){
            cout<<"程序退出"<<endl;
            break;
        }
        int ret=send(lsfd,msg.c_str(),msg.size(),0);
        if(ret<0){
            perror("send error");
            return 1;
        }
        cout<<"已发送 "<<ret<<" 字节"<<endl;
        
        char buf[1024];
        int n=recv(lsfd,buf,sizeof(buf),0);
        if(n<0){
            if(errno==EAGAIN || errno==EWOULDBLOCK)
            break;
            if(errno==EINTR)
            continue;
            perror("recv error");
            close(lsfd);
            return 1;
        }
        else if(n==0){
            cout<<"断开连接"<<endl;
            break;
        }
        else{
            string temp(buf,n);
            cout<<"接收数据 ："<<endl;
            cout<<temp<<endl;
        }
        
    }
    close(lsfd);
}