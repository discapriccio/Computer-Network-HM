#include <iostream>
#include <WinSock2.h>
#include<windows.h>
#include <time.h>
#include <unistd.h>

#pragma comment(lib,"ws2_32.lib")
using namespace std;

char recvBuf[MAXBYTE] ={0};
char sendBuf[MAXBYTE] ={0};


DWORD  dwThreadID;
HANDLE hThread;

//发送线程改为在主线程中实现
/*DWORD WINAPI send_message(LPVOID lparam)//发送信息的线程
{
    sleep(100);  //避免下方提示信息颜色受接受线程设置的影响
    cout<<"pls type in the message you want to send"<<endl;
    cout<<"(type QUIT to exit)"<<endl;
    while (1)
    {
        SOCKET sockClient = (SOCKET)(LPVOID)lparam;
        char msg[MAXBYTE] ={0};
        cin.getline(msg,MAXBYTE);
        //cout<<sendBuf<<endl;
        //time_t t = time(0);
        //char tmp[64];
        //strftime(tmp,sizeof(tmp),"%Y/%m/%d %X %A",localtime(&t));
        cout<<"cout!!!"<<endl;
        //cout<<"time:"<<tmp<<endl;
        strcpy(sendBuf, "time   ");
        strcat(sendBuf,msg);
        send(sockClient, sendBuf, MAXBYTE, 0);
        if(strcmp(msg,"QUIT")==0)
        {
            cout<<"yes"<<endl;
            break;
        }
    }
    return 0;
}*/

DWORD WINAPI recv_message(LPVOID lparam)//接受信息的线程
{
    while (1)
    {
        SOCKET sockClient = (SOCKET)(LPVOID)lparam;
        if(recv(sockClient,recvBuf,MAXBYTE,0)>0)
        {
            SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 14);
            cout<<recvBuf<<endl;
            SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
            strcpy(recvBuf,"\0");
        }
    }
    return 0;
}

int main() {
    //初始化Socket DLL，协商使用的Socket版本
    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2,2), &wsaData)!=0)
    {
        cout<<"socket dll initialization failed"<<endl;
    }

    //Create a socket that is bound to a specific transport service provider.
    //参数含义：
    //AF_INET:The Internet Protocol version 4 (IPv4) address family.
    //使用TCP协议
    //If a value of 0 is specified, the caller does not wish to specify a protocol and the service provider will choose the protocol to use.
    SOCKET sockClient = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addrSrv;
    // The sockaddr_in structure specifies the address family,
    // IP address, and port for the socket that is being bound.
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_addr.s_addr = inet_addr("127.0.0.1");
    addrSrv.sin_port = htons(27015);
    if(connect(sockClient, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR))!=SOCKET_ERROR)
    {
        //先发送姓名
        cout<<"pls type in your user name:"<<endl;
        time_t t = time(0);
        char tmp[64];
        strftime(tmp,sizeof(tmp),"[%Y/%m/%d %X] ",localtime(&t));  //加上发送消息的时间信息
        strcpy(sendBuf, tmp);
        char msg[MAXBYTE] ={0};
        cin.getline(msg,MAXBYTE);
        strcat(sendBuf,msg);
        send(sockClient, sendBuf, MAXBYTE, 0);
        //hThread[0] = CreateThread(NULL, NULL, send_message, reinterpret_cast<LPVOID>(sockClient), 0, &dwThreadID[0]);
        hThread = CreateThread(NULL, NULL, recv_message, reinterpret_cast<LPVOID>(sockClient), 0, &dwThreadID);

        //主线程作为发送消息的线程
        //sleep(0.5);  //避免下方提示信息颜色受接受线程设置的影响
        cout<<"pls type in the message you want to send"<<endl;
        cout<<"(type QUIT to exit)"<<endl;
        while (1)
        {
            char msg[MAXBYTE] ={0};
            cin.getline(msg,MAXBYTE);
            time_t t = time(0);
            char tmp[64];
            strftime(tmp,sizeof(tmp),"[%Y/%m/%d %X] ",localtime(&t));  //加上发送消息的时间信息
            strcpy(sendBuf, tmp);
            strcat(sendBuf,msg);
            send(sockClient, sendBuf, MAXBYTE, 0);
            if(strcmp(msg,"QUIT")==0)
            {
                break;
            }
        }
    }

    return 0;
}
