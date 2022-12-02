#include <iostream>
#include <WinSock2.h>
#include<windows.h>
#include<string>
#include<vector>
#include<algorithm>
#pragma comment(lib,"ws2_32.lib")
using namespace std;

int nSize = sizeof(SOCKADDR);
const int nameLength=20;
DWORD  dwThreadID;

struct Client{
    SOCKET cliSocket;
    SOCKADDR cliAddr;
    char name[nameLength]={0};
};
vector<Client*> clients;

DWORD WINAPI receive_message(LPVOID lparam)//服务端收到消息的线程
{
    Client client = *(Client*)(LPVOID)lparam;
    char recvBuf[MAXBYTE] ={0};
    if(recv(client.cliSocket,recvBuf,MAXBYTE,0)>0)
    {
        //cout<<"ori recvBuf: "<<recvBuf<<" ;"<<endl;
        char msg[MAXBYTE]={0};
        strncpy(msg, recvBuf + 23 - 1, 4);
        strcpy(client.name,msg);
        strcat(recvBuf," is in");
        cout<<recvBuf<<endl;
        for(int i=0;i<clients.size();i++)  //重名检测
        {
            if(strcmp(client.name,clients[i]->name)==0)
            {
                cout<<"Warning: name is duplicated, pls quit and change your name"<<endl;
            }
        }
        clients.push_back(&client); //新增client
        //广播给客户
        for(int i=0;i<clients.size();i++)
        {
            //if(strcmp(clients[i]->name,client.name)==0) continue;
            send(clients[i]->cliSocket,recvBuf,MAXBYTE,0);
        }
        strcpy(recvBuf,"\0");
    }
    while(1)
    {
        if(recv(client.cliSocket,recvBuf,MAXBYTE,0)>0 && strcmp(recvBuf,"\0"))  //If no error occurs, recv returns the number of bytes received
        {
            //printf("%s\n",recvBuf);
            char sendBuf[MAXBYTE] ={0};
            char msg[MAXBYTE]={0};
            strncpy(msg, recvBuf + 23 - 1, 4);
            //cout<<"msg: "<<msg<<endl;
            if(strcmp(msg,"QUIT")==0)  //client要退出
            {
                strncpy(sendBuf,recvBuf,22);
                strcat(sendBuf,client.name);
                strcat(sendBuf," is out");
                cout<<sendBuf<<endl;
                for(int i=0;i<clients.size();i++)
                {
                    send(clients[i]->cliSocket,sendBuf,MAXBYTE,0);
                }
                //clients.erase(remove(clients.begin(),clients.end(),client),clients.end()); //删除此client
                for(size_t i=0;i<clients.size();)
                {
                    if(strcmp(clients[i]->name,client.name)==0)
                    {
                        clients.erase(clients.begin()+i);
                    }
                    else
                        ++i;
                }
                break;
            }
            strcpy(sendBuf,"["); strcat(sendBuf,client.name); strcat(sendBuf,"] ");
            strcat(sendBuf,recvBuf);
            strcpy(recvBuf,"\0");
            cout<<sendBuf<<endl;
            //广播给客户
            for(int i=0;i<clients.size();i++)
            {
                //if(strcmp(clients[i]->name,client.name)==0) continue;
                send(clients[i]->cliSocket,sendBuf,MAXBYTE,0);
            }
        }
    }
}

int main() {
    //初始化Socket DLL，协商使用的Socket版本
    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2,2), &wsaData)!=0)
    {
        cout<<"socket dll initialization failed."<<endl;
    }

    //Create a socket that is bound to a specific transport service provider.
    SOCKET sockSrv = socket(AF_INET, SOCK_STREAM, 0); //AF_INET:The Internet Protocol version 4 (IPv4) address family.
                                                              //使用TCP协议
                                                              //If a value of 0 is specified, the caller does not wish to specify a protocol and the service provider will choose the protocol to use.

    //Bind the socket.
    sockaddr_in addrSrv;
    // The sockaddr_in structure specifies the address family,
    // IP address, and port for the socket that is being bound.
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_addr.s_addr = inet_addr("127.0.0.1");
    addrSrv.sin_port = htons(27015);
    bind(sockSrv, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR));

    listen(sockSrv, 5);  //5:The maximum length of the queue of pending connections.
    while(1)
    {
        Client* client=new Client;
        HANDLE* hThread = new HANDLE;
        client->cliSocket= accept(sockSrv, (SOCKADDR*)&client->cliAddr, &nSize);
        //SOCKET sockConn = accept(sockSrv, (SOCKADDR*)&client.cliAddr, &nSize);
        if(client->cliSocket!= INVALID_SOCKET)  //不是无效数据
        {
            //clients.push_back(client); //新增client
            *hThread = CreateThread(NULL, NULL,(receive_message), client, 0, &dwThreadID);
            CloseHandle(*hThread);
        }
    }



    return 0;
}
