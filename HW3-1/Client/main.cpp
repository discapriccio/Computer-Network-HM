//接收端
#include <iostream>
#include <WinSock2.h>
#include<windows.h>
#include<time.h>
#include<cmath>
#include <stdint.h>
#include<vector>
#include <fstream>

#pragma comment(lib,"ws2_32.lib")
using namespace std;

#define SYN 0b0001;
#define ACK 0b0010;
#define FIN 0b0100;
#define END 0b10000000;


enum STATE{CLOSED,SEND_SYN,ESTABLISHED,FIN_WAIT_1,FIN_WAIT_2,TIME_WAIT} state;  //客户端生命周期
int wrongCnt=0;  //校验和验证失败的包数量
int oooCnt=0;  //out of order，失序的包的数量

int timeout = 100; //0.1s
const int fileNum=4;
char* sendBuf;

SOCKET sockClient;
sockaddr_in addrSrv;
int addrSrvSize =sizeof(addrSrv);



struct Package{
    uint32_t seqNum=0;  //序列号
    uint32_t ackNum=0; //确认号
    char flags=0;  //标志位
    uint16_t checkSum=0;  //校验和
    uint32_t dataLength=0;  //数据长度
    char* data;  //数据

    int getByteLength()  //返回转化为char*后的字节长度
    {
        return 4+4+1+2+4+dataLength;
    }

    ~Package()
    {

    }
};

int packageLengthBeforeData = 4+4+1+2+4;

uint32_t charStarToUint32(char* c, int i)  //将c的第i个位置开始的四个字节变为uint32_t型
{
    uint32_t ret=0;
    for(int j=0;j<4;j++)
    {
        ret+=uint8_t(c[i]);
        if(j!=3) ret=ret<<8;
        i++;
    }
    return ret;
}
uint16_t charStarToUint16(char* c, int i)  //将c的第i个位置开始的四个字节变为uint16_t型
{
    uint16_t ret=0;
    for(int j=0;j<2;j++)
    {
        ret+=uint8_t(c[i]);
        if(j!=1) ret=ret<<8;
        i++;
    }
    return ret;
}

void uint32ToCharStar(uint32_t num,char* c, int & i)  //将uint32_t型变为char*，从c的第i个位置开始填入
{
    for(int j=3;j>=0;j--)
    {
        c[i+j]=char(num%256);
        num=num>>8;
    }
    i+=4;
}
void uint16ToCharStar(uint32_t num,char* c, int & i)  //将uint16_t型变为char*，从c的第i个位置开始填入
{
    for(int j=1;j>=0;j--)
    {
        c[i+j]=char(num%256);
        num=num>>8;
    }
    i+=2;
}

char* toCharStar(Package p)  //将Package转化为char*
{
    char* ret=new char[p.getByteLength()];
    int i=0;
    uint32ToCharStar(p.seqNum,ret,i);
    uint32ToCharStar(p.ackNum,ret,i);
    ret[i++]=p.flags;
    uint16ToCharStar(p.checkSum,ret,i);
    uint32ToCharStar(p.dataLength,ret,i);
    for(int j=0;j<p.dataLength;j++) ret[i+j]=p.data[j];
    return ret;
}

int bufLen = 16384;
char* recvBuf=new char[bufLen];
uint32_t getSeqNum()
{
    return charStarToUint32(recvBuf,0);
}
uint32_t getAckNum()
{
    return charStarToUint32(recvBuf,4);
}
char getFlag()
{
    return recvBuf[8];
}
uint16_t getCheckSum()
{
    return charStarToUint16(recvBuf,9);
}
uint32_t getDataLength()
{
    return charStarToUint32(recvBuf,11);
}
string toString(char* c, int start, int len)
{
    string ret;
    for(int i=0;i<len;i++) ret+=c[start+i];
    return ret;
}

uint16_t cal_checkSum(Package p)  //对Package计算校验和
{
    int cnt=7;
    uint32_t d=0x00010000;
    uint16_t* buf = new uint16_t [7];
    buf[0]=p.seqNum<<16; buf[1]=p.seqNum%d;
    buf[2]=p.ackNum<<16; buf[3]=p.ackNum%d;
    buf[4]=(uint16_t)p.flags;
    buf[5]=p.dataLength<<16; buf[6]=p.dataLength%d;
    register u_long sum = 0;
    while(cnt--)
    {
        sum+=*buf++;
        if(sum & 0xFFFF0000)
        {
            sum &= 0xFFFF;
            sum++;
        }
    }
    //delete buf;
    return ~(sum&0xFFFF);
}
uint16_t cal_checkSum()  //对recvBuf计算校验和
{
    int cnt=7;
    uint32_t d=0x00010000;
    uint16_t* buf = new uint16_t [7];
    buf[0]=getSeqNum()<<16; buf[1]=getSeqNum()%d;
    buf[2]=getAckNum()<<16; buf[3]=getAckNum()%d;
    buf[4]=(uint16_t)getFlag();
    buf[5]=getDataLength()<<16; buf[6]=getDataLength()%d;
    register u_long sum = 0;
    while(cnt--)
    {
        sum+=*buf++;
        if(sum & 0xFFFF0000)
        {
            sum &= 0xFFFF;
            sum++;
        }
    }
    //delete []buf;
    return ~(sum&0xFFFF);
}

void recvPkg(int& seqNum)  //停等机制，实现可靠数据传输,seqNum为期待的数据包
{
    int state=0;  //状态机状态 0——未接收到期待的数据包，1——接受到期待的数据包
    while(state!=1)
    {
        recvfrom(sockClient,recvBuf,bufLen,0,(SOCKADDR *) & addrSrv, &addrSrvSize);
        cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
            <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
        if(cal_checkSum()!=getCheckSum())
        {
            wrongCnt++;
            state=0;
            continue;
        }
        if(getSeqNum()==seqNum)
        {
            Package send;
            send.flags=0|ACK;
            send.ackNum=++seqNum;
            send.checkSum=cal_checkSum(send);
            sendBuf=toCharStar(send);
            sendto(sockClient,sendBuf,send.getByteLength(),0,(SOCKADDR*) &addrSrv, sizeof(addrSrv)); //发送ACK
            cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
            delete sendBuf;
            state=1;
        }
        else
        {
            oooCnt++;
            if(getSeqNum()<seqNum ||   //发过来的数据包是已经收到过的，回一个对应的ACK，避免发送端停滞；发过来的是超前的则不能回ACK
                 seqNum==0)            //注意seqNum=0时代表新的文件，getSeqNum()不等代表是上一个文件的残留，也需回复ack
            {
                Package send;
                send.flags=0|ACK;
                send.ackNum=getSeqNum()+1;
                send.checkSum=cal_checkSum(send);
                sendBuf=toCharStar(send);
                sendto(sockClient,sendBuf,send.getByteLength(),0,(SOCKADDR*) &addrSrv, sizeof(addrSrv)); //发送ACK
                cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                    <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
                delete sendBuf;
            }
            state=0;
        }
    }
}
int main() {

    //初始化Socket DLL，协商使用的Socket版本
    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2,2), &wsaData)!=0)
    {
        cout<<"socket dll initialization failed"<<endl;
    }

    //Create a socket that is bound to a specific transport service provider.
    //  参数含义：
    //  AF_INET:The Internet Protocol version 4 (IPv4) address family.
    //  使用UDP协议
    //  If a value of 0 is specified, the caller does not wish to specify a protocol and the service provider will choose the protocol to use.
    sockClient = socket(AF_INET, SOCK_DGRAM ,0);

    //Bind the socket.
    //  The sockaddr_in structure specifies the address family,
    //  IP address, and port for the socket that is being bound.
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_addr.s_addr = inet_addr("127.0.0.1");
//    addrSrv.sin_port = htons(27015);
    addrSrv.sin_port = htons(4001);

    srand((unsigned)time(NULL));
    //三次握手建立连接，client为主动方
    //int state=0;  //状态机状态 0——未开始建立连接，1——发送SYN，2——接到SYN+ACK并回复ACK，建连成功
    state=CLOSED;
    uint32_t seqNum;
    bool flag=1;
    while(flag)
    {
        switch(state)
        {
            case CLOSED:
            {
                Package send;
                send.flags=0|SYN;
                send.seqNum=rand() % (uint32_t)pow(2,10);
                seqNum=send.seqNum;
                send.checkSum=cal_checkSum(send);
                sendBuf=toCharStar(send);
                sendto(sockClient,sendBuf,send.getByteLength(),0,(SOCKADDR*) &addrSrv, sizeof(addrSrv)); //发送ACK
                cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                    <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
                delete sendBuf;
                state=SEND_SYN;
                break;
            }
            case SEND_SYN:
            {
                setsockopt(sockClient,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
                int ret = recvfrom(sockClient,recvBuf,bufLen,0,(SOCKADDR *) & addrSrv, &addrSrvSize);
                if(ret==0)  //超时重传
                {
                    state=CLOSED;
                    break;
                }
                cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
                    <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
                if((int(getFlag())|0)==0b0011 && getAckNum()==seqNum+1 && cal_checkSum()==getCheckSum())  //收到SYN和ACK
                {
                    Package send;
                    send.flags=0|ACK;
                    send.seqNum=++seqNum;
                    send.ackNum=getSeqNum()+1;
                    send.checkSum=cal_checkSum(send);
                    sendBuf=toCharStar(send);
                    sendto(sockClient,sendBuf,send.getByteLength(),0,(SOCKADDR*) &addrSrv, sizeof(addrSrv)); //发送ACK
                    cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                        <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
                    delete sendBuf;
                    state=ESTABLISHED;
                }
                else
                    state=CLOSED;
                break;
            }
            case ESTABLISHED:
            {
                flag=0;
                break;
            }
            default: ;
        }
    }

    cout<<"[LOG]: connection established."<<endl;

    //接受文件
    int cnt=0;
    ofstream ofs;
    int expectSeqNum=0;
    while(cnt<fileNum)
    {
        recvPkg(expectSeqNum);
        if(recvBuf[packageLengthBeforeData]=='@'&& getSeqNum()==0)  //新文件
        {
            cout<<"new file!"<<endl;
            ofs.open("recv/"+toString(recvBuf,packageLengthBeforeData+1,getDataLength()),ios::trunc|ios::binary|ios::out);
            if(!ofs.is_open())
            {
                cout<<"file create failed"<<endl;
            }

            continue;
        }
        int len=getDataLength();
        char* writeBuffer = new char[len];
        for(int i=0;i<len;i++) writeBuffer[i]=recvBuf[packageLengthBeforeData+i];
        ofs.write(writeBuffer,len);
        delete[] writeBuffer;
        if((uint8_t (getFlag())|0)==0b10000000) //当前slice是当前文件的最后一片
        {
            //int len=getDataLength();
            //char* writeBuffer = new char[len];
            //for(int i=0;i<len;i++) writeBuffer[i]=recvBuf[packageLengthBeforeData+i];
            //ofs.write(writeBuffer,len);
            //delete[] writeBuffer;
            cout<<"close file"<<endl;
            ofs.close();
            cnt++;
            expectSeqNum=0;
        }
    }

    cout<<"wrong package cnt:"<<wrongCnt<<endl;
    cout<<"out of order package cnt:"<<oooCnt<<endl;

    //四次挥手
    while(state!=CLOSED)
    {
        switch(state)
        {
            case ESTABLISHED:
            {
                Package send;
                send.flags=0|FIN;
                send.seqNum=rand() % (uint32_t)pow(2,10);
                seqNum=send.seqNum;
                send.checkSum=cal_checkSum(send);
                sendBuf=toCharStar(send);
                sendto(sockClient,sendBuf,send.getByteLength(),0,(SOCKADDR*) &addrSrv, sizeof(addrSrv)); //发送ACK
                cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                    <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
                delete sendBuf;
                state=FIN_WAIT_1;
                break;
            }
            case FIN_WAIT_1:
            {
                recvfrom(sockClient,recvBuf,bufLen,0,(SOCKADDR *) & addrSrv, &addrSrvSize);
                cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
                    <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
                if((int(getFlag())|0)==0b0010 && getAckNum()==seqNum+1 && cal_checkSum()==getCheckSum())  //收到ACK
                {
                    state=FIN_WAIT_2;
                }
                else
                    state=ESTABLISHED;
                break;
            }
            case FIN_WAIT_2:
            {
                recvfrom(sockClient,recvBuf,bufLen,0,(SOCKADDR *) & addrSrv, &addrSrvSize);
                cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
                    <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
                if((int(getFlag())|0)==0b0100 && cal_checkSum()==getCheckSum())  //收到FIN
                {
//                    state=TIME_WAIT;
                    state=CLOSED;
                    //回复ACK
                    Package send;
                    send.flags=0|ACK;
                    send.seqNum=getSeqNum()+1;
                    send.checkSum=cal_checkSum(send);
                    sendBuf=toCharStar(send);
                    sendto(sockClient,sendBuf,send.getByteLength(),0,(SOCKADDR*) &addrSrv, sizeof(addrSrv)); //发送ACK
                    cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                        <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
                    delete sendBuf;
                }
                else
                    state=FIN_WAIT_2;
                break;
            }
            case TIME_WAIT:
            {
                setsockopt(sockClient,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
                int ret = recvfrom(sockClient,recvBuf,bufLen,0,(SOCKADDR *) & addrSrv, &addrSrvSize);
                if(ret==0)  //超时，没有新的包到来
                    state = CLOSED;
                else
                {
                    cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
                        <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
                    state=FIN_WAIT_2;
                }
            }
        }
    }

    cout<<"[LOG:] connection dismissed."<<endl;
    cout<<"Bye"<<endl;
    return 0;
}
