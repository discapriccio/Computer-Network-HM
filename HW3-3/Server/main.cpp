//发送端
#include <iostream>
#include <WinSock2.h>
#include<windows.h>
#include<time.h>
#include<cmath>
#include <stdint.h>
#include<fstream>
#include<vector>
#include<queue>
#include <mutex>

#pragma comment(lib,"ws2_32.lib")
using namespace std;

#define SYN 0b0001;
#define ACK 0b0010;
#define FIN 0b0100;
#define STA 0b00001000;  //文件第一个包
#define END 0b10000000;  //文件最后一个包

enum PROTOCAL{stopAndWait,GBN_oneThread,GBN_mulThread,GBN_oneThread_RENO,GBN_mulThread_RENO} protocal=GBN_oneThread_RENO;
bool coutOn=1;

long long head, tail, freq;  //timers
uint32_t sendByteCnt=0;

int timeout=1000;  //1s
int timeOutCnt=0;  //超时重传次数
const int fileNum=4;
string dir="test/";
string files[4]={"1.jpg","2.jpg","3.jpg","helloworld.txt"};
const int sliceSize=10000;

enum STATE{CLOSED, LISTEN, SYN_RCVD, ESTABLISHED, CLOSE_WAIT, LAST_ACK} state;  //服务端生命周期

SOCKET sockSrv;
sockaddr_in addrClient;
int addrClientSize = sizeof (addrClient);

char* sendBuf;
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
//        cout<<uint8_t(c[i])<<endl;
//        cout<<"int:"<<int(c[i])<<endl;
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
    //delete []buf;
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

int sendPkg(Package send)  //停等机制，保证可靠传输  返回值：0——正常，1——接收到FIN，应进入挥手状态
{
    int state=0;  //状态机状态，0——尚未发送数据包，1——等待ACK，2——收到ACK
    while(state!=2)
    {
        switch(state)
        {
            case 0:
            {
                sendBuf=toCharStar(send);
                sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
                sendByteCnt+=send.getByteLength();
                if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                    <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
                state=1;
                break;
            }
            case 1:
            {
                setsockopt(sockSrv,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
                int ret=recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
//                recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
                if((int(getFlag())|0)==0b0100)  //FIN
                {
                    return 1;
                }
                if(coutOn) cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
                    <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;

                if(ret==0)  //超时重传
                {
                    timeOutCnt++;
                    state=0;
                    break;
                }
               if((int(getFlag())|0)==0b0010 && getAckNum()==send.seqNum+1 && cal_checkSum()==getCheckSum())  //收到ACK
                {
                    state=2;
                }
                else
                    state=0;
                break;
            }
            default: ;
        }
    }
    delete sendBuf;
    delete send.data;
    return 0;
}
//GBN
DWORD  dwThreadID;
HANDLE hThread;
uint32_t base=0;
uint32_t nextseqnum=0;
const uint32_t N=300;
queue<Package> sendBufQueue;
vector<Package> sendBufQueueVec;

long long headGBN, tailGBN, freqGBN;  //timers
bool isTimerOn=1;  //1-定时器开着；0-关着

int recvPatiency=100;  //0.1s

char* recvBuf2=new char[bufLen];
//流水线
int rdt_sendPkg_GBN_oneThread(Package send)
{
    //接受ACK
    setsockopt(sockSrv,SOL_SOCKET,SO_RCVTIMEO,(char*)&recvPatiency,sizeof(recvPatiency));
    int recvRet=recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
    if(coutOn) cout<<"recvRet:"<<recvRet<<endl;
    if(coutOn) cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
        <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
    if((int(getFlag())|0)==0b0100)  //FIN
    {
        return 1;
    }
//    if((int(getFlag())|0)==0b0010 && cal_checkSum()==getCheckSum() && strcmp(recvBuf2,recvBuf)!=0)  //收到ACK
    if((int(getFlag())|0)==0b0010 && cal_checkSum()==getCheckSum() && recvRet!=-1)  //收到ACK
    {
        if(getAckNum()<=nextseqnum)  //防止上一个文件的ack被误接受-->防止对握手的ack被误接收
        {
            if(base==getAckNum())
            {
                isTimerOn=1;
                QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
                QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
                if(coutOn) cout<<"hello"<<endl;
            }
            base=getAckNum()+1;
            if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"base:"<<base<<"\t""nextseqnum:"<<nextseqnum<<endl;
            while(sendBufQueue.size()>0 && sendBufQueue.front().seqNum<=getAckNum())  sendBufQueue.pop();
            if(base==nextseqnum) isTimerOn=0;
        }
    }
    strcpy(recvBuf2,recvBuf);

    //判断定时器
    QueryPerformanceCounter((LARGE_INTEGER*)&tailGBN);
    //cout<<"freqGBN:"<<freqGBN<<endl;
    //cout<<"tailGBN-headGBN) * 1000.0 / freqGBN:"<<(tailGBN-headGBN) * 1000.0 / freqGBN<<endl;
    if(( tailGBN-headGBN) * 1000.0 / freqGBN > timeout && isTimerOn)  //ms
//    if(( tailGBN-headGBN) * 1000.0  > timeout && isTimerOn)  //ms
    {
        if(coutOn) cout<<"time out"<<endl;
        timeOutCnt++;
        int iter=sendBufQueue.size();
        for(int i=0;i<iter;i++)
        {
            Package send=sendBufQueue.front();
            sendBufQueue.pop();
            sendBufQueue.push(send);  //queue没有迭代器，于是用这种pop再push的方法完成，对其中所有元素重传
            sendBuf=toCharStar(send);
            sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
            sendByteCnt+=send.getByteLength();
            if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
        }
        isTimerOn=1;
        QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
        QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
    }

    if(nextseqnum>=base+N) return 2;  //发送窗口已满
    sendBufQueue.push(send);
    sendBuf=toCharStar(send);
    sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
    sendByteCnt+=send.getByteLength();
    if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
        <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
    if(base==nextseqnum)
    {
        isTimerOn=1;
        QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
        QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
    }
    nextseqnum++;
    if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"base:"<<base<<"\t""nextseqnum:"<<nextseqnum<<endl;
}

mutex mtx;
int rdt_sendPkg_GBN_mulThread(Package send)
{
    if(nextseqnum>=base+N) return 2;  //发送窗口已满
    mtx.lock();
    sendBufQueueVec.push_back(send);
    mtx.unlock();
    sendBuf=toCharStar(send);
    sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
    sendByteCnt+=send.getByteLength();
    if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
        <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
    if(base==nextseqnum)
    {
        isTimerOn=1;
        QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
        QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
    }
    nextseqnum++;
    if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"base:"<<base<<"\t""nextseqnum:"<<nextseqnum<<endl;
}

DWORD WINAPI recv_gbn(LPVOID lparam)//接受信息的线程
{
    while(1)
    {
        //判断定时器
        QueryPerformanceCounter((LARGE_INTEGER*)&tailGBN);
        //cout<<"tailGBN-headGBN:"<<(tailGBN-headGBN) * 1000.0 / freq<<"\t"<<"timeout:"<<timeout<<endl;
        if((tailGBN-headGBN) * 1000.0 / freq > timeout && isTimerOn)  //ms
        {
            //if(coutOn) cout<<"sendBufQueueVec.size:"<<sendBufQueueVec.size()<<endl;
            mtx.lock();
            if(sendBufQueueVec.size()>0)
            {
                if(coutOn)cout<<"time out"<<endl;
                timeOutCnt++;
                int iter=sendBufQueueVec.size();
                for(int i=0;i<iter;i++)
                {
                    Package send=sendBufQueueVec[i];
                    //sendBufQueue.pop();
                    //sendBufQueue.push(send);  //queue没有迭代器，于是用这种pop再push的方法完成，对其中所有元素重传
                    sendBuf=toCharStar(send);
                    sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
                    sendByteCnt+=send.getByteLength();
                    if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                                   <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
                }
            }
            mtx.unlock();
            isTimerOn=1;
            QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
            QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
        }

        //接受ACK
        //setsockopt(sockSrv,SOL_SOCKET,SO_RCVTIMEO,(char*)&recvPatiency,sizeof(recvPatiency));
        recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
        if(coutOn) cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
            <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
        if((int(getFlag())|0)==0b0100)  //FIN
        {
            return 1;
        }
        if((int(getFlag())|0)==0b0010 && cal_checkSum()==getCheckSum())  //收到ACK
        {
            if(getAckNum()<=nextseqnum)  //防止上一个文件的ack被误接受-->防止对握手的ack被误接收
            {
                if(base==getAckNum())
                {
                    isTimerOn=1;
                    QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
                    QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
                    if(coutOn) cout<<"hello"<<endl;
                }
                base=getAckNum()+1;
                if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"base:"<<base<<"\t""nextseqnum:"<<nextseqnum<<endl;
                mtx.lock();
                while(sendBufQueueVec.size()>0 && sendBufQueueVec[0].seqNum<=getAckNum())
                {
                    vector< Package >::iterator k = sendBufQueueVec.begin();
                    sendBufQueueVec.erase(k); // 删除第一个元素
                }
                mtx.unlock();
                if(base==nextseqnum) isTimerOn=0;
            }
        }
    }
}

int allAccept()  //保证刚才发送的数据包被全部接收
{
    while(true)
    {
        if(sendBufQueue.size()==0) break;
        //接受ACK
        setsockopt(sockSrv,SOL_SOCKET,SO_RCVTIMEO,(char*)&recvPatiency,sizeof(recvPatiency));
        int recvRet=recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
        if(coutOn) cout<<"recvRet:"<<recvRet<<endl;
        if(coutOn) cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
            <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
        if((int(getFlag())|0)==0b0100)  //FIN
        {
            return 1;
        }
//    if((int(getFlag())|0)==0b0010 && cal_checkSum()==getCheckSum() && strcmp(recvBuf2,recvBuf)!=0)  //收到ACK
        if((int(getFlag())|0)==0b0010 && cal_checkSum()==getCheckSum() && recvRet!=-1)  //收到ACK
        {
            if(getAckNum()<=nextseqnum)  //防止上一个文件的ack被误接受-->防止对握手的ack被误接收
            {
                if(base==getAckNum())
                {
                    isTimerOn=1;
                    QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
                    QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
                    if(coutOn) cout<<"hello"<<endl;
                }
                base=getAckNum()+1;
                if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"base:"<<base<<"\t""nextseqnum:"<<nextseqnum<<endl;
                while(sendBufQueue.size()>0 && sendBufQueue.front().seqNum<=getAckNum())  sendBufQueue.pop();
                if(base==nextseqnum) isTimerOn=0;
            }
        }
        strcpy(recvBuf2,recvBuf);

        //判断定时器
        QueryPerformanceCounter((LARGE_INTEGER*)&tailGBN);
        //cout<<"freqGBN:"<<freqGBN<<endl;
        //cout<<"tailGBN-headGBN) * 1000.0 / freqGBN:"<<(tailGBN-headGBN) * 1000.0 / freqGBN<<endl;
        if(( tailGBN-headGBN) * 1000.0 / freqGBN > timeout && isTimerOn)  //ms
//    if(( tailGBN-headGBN) * 1000.0  > timeout && isTimerOn)  //ms
        {
            if(coutOn) cout<<"time out"<<endl;
            timeOutCnt++;
            int iter=sendBufQueue.size();
            for(int i=0;i<iter;i++)
            {
                Package send=sendBufQueue.front();
                sendBufQueue.pop();
                sendBufQueue.push(send);  //queue没有迭代器，于是用这种pop再push的方法完成，对其中所有元素重传
                sendBuf=toCharStar(send);
                sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
                sendByteCnt+=send.getByteLength();
                if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                    <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
            }
            isTimerOn=1;
            QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
            QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
        }
    }
}
int allAccept_multhread()  //保证刚才发送的数据包被全部接收
{
    while(true)
    {
        if(sendBufQueueVec.size()==0) break;
        //接受ACK
        setsockopt(sockSrv,SOL_SOCKET,SO_RCVTIMEO,(char*)&recvPatiency,sizeof(recvPatiency));
        int recvRet=recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
        if(coutOn) cout<<"recvRet:"<<recvRet<<endl;
        if(coutOn) cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
                       <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
        if((int(getFlag())|0)==0b0100)  //FIN
        {
            return 1;
        }
//    if((int(getFlag())|0)==0b0010 && cal_checkSum()==getCheckSum() && strcmp(recvBuf2,recvBuf)!=0)  //收到ACK
        if((int(getFlag())|0)==0b0010 && cal_checkSum()==getCheckSum() && recvRet!=-1)  //收到ACK
        {
            if(getAckNum()<=nextseqnum)  //防止上一个文件的ack被误接受-->防止对握手的ack被误接收
            {
                if(base==getAckNum())
                {
                    isTimerOn=1;
                    QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
                    QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
                    if(coutOn) cout<<"hello"<<endl;
                }
                base=getAckNum()+1;
                if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"base:"<<base<<"\t""nextseqnum:"<<nextseqnum<<endl;
                while(sendBufQueueVec.size()>0 && sendBufQueueVec[0].seqNum<=getAckNum())
                {
                    vector< Package >::iterator k = sendBufQueueVec.begin();
                    sendBufQueueVec.erase(k); // 删除第一个元素
                }
                if(base==nextseqnum) isTimerOn=0;
            }
        }
        strcpy(recvBuf2,recvBuf);

        //判断定时器
        QueryPerformanceCounter((LARGE_INTEGER*)&tailGBN);
        //cout<<"freqGBN:"<<freqGBN<<endl;
        //cout<<"tailGBN-headGBN) * 1000.0 / freqGBN:"<<(tailGBN-headGBN) * 1000.0 / freqGBN<<endl;
        if(( tailGBN-headGBN) * 1000.0 / freqGBN > timeout && isTimerOn)  //ms
//    if(( tailGBN-headGBN) * 1000.0  > timeout && isTimerOn)  //ms
        {
            if(coutOn) cout<<"time out"<<endl;
            timeOutCnt++;
            int iter=sendBufQueueVec.size();
            for(int i=0;i<iter;i++)
            {
                Package send=sendBufQueueVec[i];
                //sendBufQueue.pop();
                //sendBufQueue.push(send);  //queue没有迭代器，于是用这种pop再push的方法完成，对其中所有元素重传
                sendBuf=toCharStar(send);
                sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
                sendByteCnt+=send.getByteLength();
                if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                               <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
            }
            isTimerOn=1;
            QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
            QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
        }
    }
}
//流水线，加入拥塞控制
double cwnd=1;  //congestion window
double ssthresh=32;  //阈值
int dupACKcount=0;  //重复ACK数量
uint32_t lastACKnum=0;  //上次收到的ack
enum CROWD_CONTROL_STATE {slow_start,crowd_avoidance,quick_recovery,} ccs_machine=slow_start;  //拥塞避免状态机
int callCnt=0;
int rdt_sendPkg_GBN_oneThread_RENO(Package send)
{
    callCnt++;
    //接受ACK
    setsockopt(sockSrv,SOL_SOCKET,SO_RCVTIMEO,(char*)&recvPatiency,sizeof(recvPatiency));
    int recvRet=recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
    if(coutOn) cout<<"recvRet:"<<recvRet<<endl;
    if(coutOn) cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
        <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
    if((int(getFlag())|0)==0b0100)  //FIN
    {
        return 1;
    }
    if((int(getFlag())|0)==0b0010 && cal_checkSum()==getCheckSum() && recvRet!=-1)  //收到ACK
    {
        if(getAckNum()<=nextseqnum)  //防止上一个文件的ack被误接受-->防止对握手的ack被误接收
        {
            if(base==getAckNum())
            {
                isTimerOn=1;
                QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
                QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
                if(coutOn) cout<<"hello"<<endl;
            }
            base=getAckNum()+1;
            if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"base:"<<base<<"\t""nextseqnum:"<<nextseqnum<<endl;
            while(sendBufQueue.size()>0 && sendBufQueue.front().seqNum<=getAckNum())  sendBufQueue.pop();
            if(base==nextseqnum) isTimerOn=0;

            // 拥塞控制
            switch(ccs_machine)
            {
                case slow_start:
                {
                    if(getAckNum()==lastACKnum)  //重复ACK
                        dupACKcount++;
                    else  //新ACK
                    {
                        dupACKcount=0;
                        cwnd=cwnd+1;
                    }
                    lastACKnum=getAckNum();
                    if(dupACKcount==3)
                    {
                        ssthresh=(cwnd/2.0);
                        cwnd=ssthresh+3;
                        ccs_machine=quick_recovery;
                    }
                    if(cwnd>ssthresh) ccs_machine=crowd_avoidance;
                    break;
                }
                case crowd_avoidance:
                {
                    if(getAckNum()==lastACKnum)  //重复ACK
                        dupACKcount++;
                    else  //新ACK
                    {
                        dupACKcount=0;
                        if(coutOn) cout<<"***"<<cwnd<<endl;
                        cwnd=cwnd+1*(1/cwnd);
                    }
                    lastACKnum=getAckNum();
                    if(dupACKcount==3)
                    {
                        ssthresh=(cwnd/2.0);
                        cwnd=ssthresh+3;
                        ccs_machine=quick_recovery;
                    }
                    break;
                }
                case quick_recovery:
                {
                    if(getAckNum()==lastACKnum)  //重复ACK
                        cwnd=cwnd+1;
                    else  //新ACK
                    {
                        dupACKcount=0;
                        cwnd=ssthresh;
                        ccs_machine=crowd_avoidance;
                    }
                    lastACKnum=getAckNum();
                    if(cwnd>ssthresh) ccs_machine=crowd_avoidance;
                    break;
                }
            }
            if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"ccs_machine: "<<ccs_machine<<"\t"<<"cwnd:"<<cwnd<<"\t""ssthresh:"<<ssthresh<<endl;
        }
    }

    //判断定时器
    QueryPerformanceCounter((LARGE_INTEGER*)&tailGBN);
    //cout<<"freqGBN:"<<freqGBN<<endl;
    //cout<<"tailGBN-headGBN) * 1000.0 / freqGBN:"<<(tailGBN-headGBN) * 1000.0 / freqGBN<<endl;
    if(( tailGBN-headGBN) * 1000.0 / freqGBN > timeout && isTimerOn && callCnt>1)  //ms
    {
        if(coutOn) cout<<"time out"<<endl;
        timeOutCnt++;
        int iter=sendBufQueue.size();
        for(int i=0;i<iter;i++)
        {
            Package send=sendBufQueue.front();
            sendBufQueue.pop();
            sendBufQueue.push(send);  //queue没有迭代器，于是用这种pop再push的方法完成，对其中所有元素重传
            sendBuf=toCharStar(send);
            sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
            sendByteCnt+=send.getByteLength();
            if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
        }
        isTimerOn=1;
        QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
        QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer

        //拥塞控制
        switch(ccs_machine)
        {
            case slow_start:
            {
                ssthresh=(cwnd/2.0);
                cwnd=1;
                dupACKcount=0;
                break;
            }
            case crowd_avoidance:
            {
                ssthresh=(cwnd/2.0);
                cwnd=1;
                dupACKcount=0;
                ccs_machine=slow_start;
                break;
            }
            case quick_recovery:
            {
                ssthresh=(cwnd/2.0);
                cwnd=1;
                dupACKcount=0;
                ccs_machine=slow_start;
                break;
            }
        }
        if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"ccs_machine: "<<ccs_machine<<"\t"<<"cwnd:"<<cwnd<<"\t""ssthresh:"<<ssthresh<<endl;
    }

    if(nextseqnum >= base + min(int(N),int(floor(cwnd))) ) return 2;  //发送窗口已满
    sendBufQueue.push(send);
    sendBuf=toCharStar(send);
    sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
    sendByteCnt+=send.getByteLength();
    if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
        <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
    if(base==nextseqnum)
    {
        isTimerOn=1;
        QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
        QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
    }
    nextseqnum++;
    if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"base:"<<base<<"\t""nextseqnum:"<<nextseqnum<<endl;
    if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"ccs_machine: "<<ccs_machine<<"\t"<<"cwnd:"<<cwnd<<"\t""ssthresh:"<<ssthresh<<endl;
}

int rdt_sendPkg_GBN_mulThread_RENO(Package send)
{
    if(nextseqnum > base + min(int(N),int(floor(cwnd))) ) return 2;  //发送窗口已满
    //sendBufQueue.push(send);
    mtx.lock();
    sendBufQueueVec.push_back(send);
    sendBuf=toCharStar(send);
    sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
    mtx.unlock();
    sendByteCnt+=send.getByteLength();
    if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
        <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
    if(base==nextseqnum)
    {
        isTimerOn=1;
        QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
        QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
    }
    nextseqnum++;
    if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"base:"<<base<<"\t""nextseqnum:"<<nextseqnum<<endl;
    if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"ccs_machine: "<<ccs_machine<<"\t"<<"cwnd:"<<cwnd<<"\t""ssthresh:"<<ssthresh<<endl;
}
DWORD WINAPI recv_gbn_RENO(LPVOID lparam)//接受信息的线程
{
    //int callCnt=0;
    while(1)
    {
        //callCnt++;
        //判断定时器
        QueryPerformanceCounter((LARGE_INTEGER*)&tailGBN);
        //cout<<"freqGBN:"<<freqGBN<<endl;
        //cout<<"tailGBN-headGBN) * 1000.0 / freqGBN:"<<(tailGBN-headGBN) * 1000.0 / freqGBN<<endl;
        if(( tailGBN-headGBN) * 1000.0 / freqGBN > timeout && isTimerOn )  //ms
        {
            if(coutOn) cout<<"time out"<<endl;
            timeOutCnt++;
            //int iter=sendBufQueue.size();
            mtx.lock();
            int iter = min(int(sendBufQueueVec.size()) ,min(int(N),int(floor(cwnd))));
            //int iter=min(int(sendBufQueueVec.size()),1);
            //cout<<cwnd<<endl;
            //cout<<"[LOG]: "<<"WINDOW "<<"ccs_machine: "<<ccs_machine<<"\t"<<"cwnd:"<<cwnd<<"\t""ssthresh:"<<ssthresh<<endl;
            for(int i=0;i<iter;i++)
            {
                //Package send=sendBufQueue.front();
                //sendBufQueue.pop();
                //sendBufQueue.push(send);  //queue没有迭代器，于是用这种pop再push的方法完成，对其中所有元素重传
                Package send = sendBufQueueVec[i];
                sendBuf=toCharStar(send);
                sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
                sendByteCnt+=send.getByteLength();
                if(coutOn) cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                    <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
            }
            mtx.unlock();
            isTimerOn=1;
            QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
            QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer

            //拥塞控制
            switch(ccs_machine)
            {
                case slow_start:
                {
                    ssthresh=(cwnd/2.0);
                    cwnd=1;
                    dupACKcount=0;
                    break;
                }
                case crowd_avoidance:
                {
                    ssthresh=(cwnd/2.0);
                    cwnd=1;
                    dupACKcount=0;
                    ccs_machine=slow_start;
                    break;
                }
                case quick_recovery:
                {
                    ssthresh=(cwnd/2.0);
                    cwnd=1;
                    dupACKcount=0;
                    ccs_machine=slow_start;
                    break;
                }
            }
            //if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"ccs_machine: "<<ccs_machine<<"\t"<<"cwnd:"<<cwnd<<"\t""ssthresh:"<<ssthresh<<endl;
            cout<<"[LOG]: "<<"WINDOW "<<"ccs_machine: "<<ccs_machine<<"\t"<<"cwnd:"<<cwnd<<"\t""ssthresh:"<<ssthresh<<endl;
        }

        //cout<<"*******"<<endl;
        //接受ACK
        //setsockopt(sockSrv,SOL_SOCKET,SO_RCVTIMEO,(char*)&recvPatiency,sizeof(recvPatiency));
        int recvRet=recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
        if(coutOn) cout<<"recvRet:"<<recvRet<<endl;
        if(coutOn) cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
            <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
        if((int(getFlag())|0)==0b0100)  //FIN
        {
            return 1;
        }
        if((int(getFlag())|0)==0b0010 && cal_checkSum()==getCheckSum() && recvRet!=-1)  //收到ACK
        {
            if(getAckNum()<=nextseqnum)  //防止上一个文件的ack被误接受-->防止对握手的ack被误接收
            {
                if(base==getAckNum())
                {
                    isTimerOn=1;
                    QueryPerformanceFrequency((LARGE_INTEGER*)&freqGBN);
                    QueryPerformanceCounter((LARGE_INTEGER*)&headGBN);   //start timer
                    if(coutOn)cout<<"hello"<<endl;
                }
                base=getAckNum()+1;
                if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"base:"<<base<<"\t""nextseqnum:"<<nextseqnum<<endl;
                mtx.lock();
                while(sendBufQueueVec.size()>0 && sendBufQueueVec[0].seqNum<=getAckNum())
                {
                    vector< Package >::iterator k = sendBufQueueVec.begin();
                    sendBufQueueVec.erase(k); // 删除第一个元素
                }
                mtx.unlock();
                if(base==nextseqnum) isTimerOn=0;

                // 拥塞控制
                switch(ccs_machine)
                {
                    case slow_start:
                    {
                        if(getAckNum()==lastACKnum)  //重复ACK
                            dupACKcount++;
                        else  //新ACK
                        {
                            dupACKcount=0;
                            cwnd=cwnd+1;
                        }
                        lastACKnum=getAckNum();
                        if(dupACKcount==3)
                        {
                            ssthresh=(cwnd/2.0);
                            cwnd=ssthresh+3;
                            ccs_machine=quick_recovery;
                        }
                        if(cwnd>ssthresh) ccs_machine=crowd_avoidance;
                        break;
                    }
                    case crowd_avoidance:
                    {
                        if(getAckNum()==lastACKnum)  //重复ACK
                            dupACKcount++;
                        else  //新ACK
                        {
                            dupACKcount=0;
                            if(coutOn) cout<<"***"<<cwnd<<endl;
                            cwnd=cwnd+1*(1/cwnd);
                        }
                        lastACKnum=getAckNum();
                        if(dupACKcount==3)
                        {
                            ssthresh=(cwnd/2.0);
                            cwnd=ssthresh+3;
                            ccs_machine=quick_recovery;
                        }
                        break;
                    }
                    case quick_recovery:
                    {
                        if(getAckNum()==lastACKnum)  //重复ACK
                            cwnd=cwnd+1;
                        else  //新ACK
                        {
                            dupACKcount=0;
                            cwnd=ssthresh;
                            ccs_machine=crowd_avoidance;
                        }
                        lastACKnum=getAckNum();
                        if(cwnd>ssthresh) ccs_machine=crowd_avoidance;
                        break;
                    }
                }
                //if(coutOn) cout<<"[LOG]: "<<"WINDOW "<<"ccs_machine: "<<ccs_machine<<"\t"<<"cwnd:"<<cwnd<<"\t""ssthresh:"<<ssthresh<<endl;
                cout<<"[LOG2]: "<<"WINDOW "<<"ccs_machine: "<<ccs_machine<<"\t"<<"cwnd:"<<cwnd<<"\t""ssthresh:"<<ssthresh<<endl;
            }
        }
    }
}
uint32_t seqNum=0;
int sendFile(string path)
{
    //base=0;
    //nextseqnum=0;
    ifstream ifs;
    ifs.open(path.c_str(),ios::binary);
    if(!ifs.is_open())
    {
        cout<<"file open failed"<<endl;
        return 0;
    }
    cout<<"new file"<<endl;
    //uint32_t seqNum=0;
    //发送@+文件名，seqNum=0
    Package send;
    send.seqNum=seqNum;
    send.dataLength=1+strlen(path.c_str());
    send.data=new char[send.dataLength];
    strcpy(send.data,"@");
    strcat(send.data,path.substr(dir.length(),path.length()).c_str());  //将文件夹名去掉
    send.flags=0|STA;
    send.checkSum=cal_checkSum(send);
    switch (protocal) {
        case stopAndWait:
        {
            if(sendPkg(send)==1)
            {
                return 1;  //FIN，该进入四次挥手
            }
            break;
        }
        case GBN_oneThread:
        {
            while(true)
            {
                int ret= rdt_sendPkg_GBN_oneThread(send);
                if(ret==1) return 1;
                if(ret!=2) break;
            }
            break;
        }
        case GBN_mulThread:
        {
            while(true)
            {
                int ret= rdt_sendPkg_GBN_mulThread(send);
                if(ret==1) return 1;
                if(ret!=2) break;
            }
            break;
        }
        case GBN_oneThread_RENO:
        {
            while(true)
            {
                int ret= rdt_sendPkg_GBN_oneThread_RENO(send);
                if(ret==1) return 1;
                if(ret!=2) break;
            }
            break;
        }
        case GBN_mulThread_RENO:
        {
            while(true)
            {
                int ret= rdt_sendPkg_GBN_mulThread_RENO(send);
                if(ret==1) return 1;
                if(ret!=2) break;
            }
            break;
        }
    }


    //按块发送文件
    int curP=0;
    ifs.seekg(0, std::ios::end);
    int len = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    while(curP+sliceSize<len)
    {
        send.data=new char[sliceSize];
        ifs.read(send.data,sliceSize);
        send.seqNum=(++seqNum);
        send.dataLength=sliceSize;
        send.flags=0;
        send.checkSum=cal_checkSum(send);

        switch (protocal) {
            case stopAndWait:
            {
                if(sendPkg(send)==1)
                {
                    return 1;  //FIN，该进入四次挥手
                }
                break;
            }
            case GBN_oneThread:
            {
                while(true)
                {
                    int ret= rdt_sendPkg_GBN_oneThread(send);
                    if(ret==1) return 1;
                    if(ret!=2) break;
                }
                break;
            }
            case GBN_mulThread:
            {
                while(true)
                {
                    int ret= rdt_sendPkg_GBN_mulThread(send);
                    if(ret==1) return 1;
                    if(ret!=2) break;
                }
                break;
            }
            case GBN_oneThread_RENO:
            {
                while(true)
                {
                    int ret= rdt_sendPkg_GBN_oneThread_RENO(send);
                    if(ret==1) return 1;
                    if(ret!=2) break;
                }
                break;
            }
            case GBN_mulThread_RENO:
            {
                while(true)
                {
                    int ret= rdt_sendPkg_GBN_mulThread_RENO(send);
                    if(ret==1) return 1;
                    if(ret!=2) break;
                }
                break;
            }
        }
        curP+=sliceSize;
    }
    //发送最后一个slice的文件
    int lastLen=len<sliceSize? len:len-curP;
    send.data=new char[lastLen];
    ifs.read(send.data,lastLen);
    send.seqNum=++seqNum;
    send.flags=0|END;
    send.dataLength=lastLen;
    send.checkSum=cal_checkSum(send);
    switch (protocal) {
        case stopAndWait:
        {
            if(sendPkg(send)==1)
            {
                return 1;  //FIN，该进入四次挥手
            }
            break;
        }
        case GBN_oneThread:
        {
            while(true)
            {
                int ret= rdt_sendPkg_GBN_oneThread(send);
                if(ret==1) return 1;
                if(ret!=2) break;
            }
            break;
        }
        case GBN_mulThread:
        {
            while(true)
            {
                int ret= rdt_sendPkg_GBN_mulThread(send);
                if(ret==1) return 1;
                if(ret!=2) break;
            }
            break;
        }
        case GBN_oneThread_RENO:
        {
            while(true)
            {
                int ret= rdt_sendPkg_GBN_oneThread_RENO(send);
                if(ret==1) return 1;
                if(ret!=2) break;
            }
            break;
        }
        case GBN_mulThread_RENO:
        {
            while(true)
            {
                int ret= rdt_sendPkg_GBN_mulThread_RENO(send);
                if(ret==1) return 1;
                if(ret!=2) break;
            }
            break;
        }
    }
    seqNum++;
    switch (protocal)
    {
        case stopAndWait:
            break;
        case GBN_mulThread_RENO:
        case GBN_mulThread:
            allAccept_multhread();
            break;
        default: {
            allAccept();
            break;
        }
    }
    return 0;
}


int main() {



    //初始化Socket DLL，协商使用的Socket版本
    WSADATA wsaData;
    if(WSAStartup(  MAKEWORD(2,2), &wsaData)!=0)
    {
        cout<<"socket dll initialization failed."<<endl;
    }

    //Create a socket that is bound to a specific transport service provider.
    //  参数含义：
    //  AF_INET:The Internet Protocol version 4 (IPv4) address family.
    //  使用UDP协议
    //  If a value of 0 is specified, the caller does not wish to specify a protocol and the service provider will choose the protocol to use.
    sockSrv = socket(AF_INET, SOCK_DGRAM, 0);

    //Bind the socket.
    sockaddr_in addrSrv;
    //  The sockaddr_in structure specifies the address family,
    //  IP address, and port for the socket that is being bound.
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_addr.s_addr = inet_addr("127.0.0.1");
    addrSrv.sin_port = htons(27015);
    bind(sockSrv, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR));


    //三次握手建立连接，server为被动方
    //int state=0;  //状态机的状态 0——未接受到建连请求，1——接收到SYN并发送ACK和SYN，2——接收到ACK建连成功
    state=LISTEN;
    uint32_t seqNum;
    bool flag=1;
    while(flag)
    {
        switch(state)
        {
            case LISTEN:
            {
                recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
                cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
                    <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
                if((int(getFlag())|0)==1 && cal_checkSum()==getCheckSum())  //收到SYN
                {
                    state=SYN_RCVD;
                    Package send;
                    send.ackNum=getSeqNum()+1;
                    send.flags=0| SYN;
                    send.flags = send.flags|ACK;
                    send.seqNum=rand() % (int)pow(2,10);
                    seqNum=send.seqNum;
                    send.checkSum=cal_checkSum(send);
                    sendBuf=toCharStar(send);
                    sendByteCnt+=send.getByteLength();
                    sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
                    cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                        <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
                    delete sendBuf;
                }
                else
                    state=LISTEN;
                break;
            }
            case SYN_RCVD:
            {
                recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
                cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
                    <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
                if((int(getFlag())|0)==0b0010 && getAckNum()==seqNum+1 && cal_checkSum()==getCheckSum())  //收到ACK
                {
                    state=ESTABLISHED;
                }
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

    QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
    QueryPerformanceCounter((LARGE_INTEGER*)&head);

    switch (protocal) {
        case GBN_mulThread:
        {
            //创建接收辅助线程
            hThread = CreateThread(NULL, NULL, recv_gbn, reinterpret_cast<LPVOID>(sockSrv), 0, &dwThreadID);  //GBN多线程 接收线程
            break;
        }
        case GBN_mulThread_RENO:
        {
            //创建接收辅助线程
            hThread = CreateThread(NULL, NULL, recv_gbn_RENO, reinterpret_cast<LPVOID>(sockSrv), 0, &dwThreadID);  //GBN多线程 接收线程
            break;
        }
    }

    //传输数据
    for(int i=0;i<fileNum;i++)
    {
        if(sendFile(dir+files[i])==1)
        {
            state=CLOSE_WAIT;
            break;
        }
    }

    cout<<"time out cnt:"<<timeOutCnt<<endl;

    //四次挥手
    while(state!=CLOSED)
    {
        switch(state)
        {
            case ESTABLISHED:
            {
                recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
                cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
                    <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
                if((int(getFlag())|0)==0b0100 && cal_checkSum()==getCheckSum())  //收到FIN
                {
                    state=CLOSE_WAIT;
                    Package send;
                    send.ackNum=getSeqNum()+1;
                    send.flags=0| ACK;
                    send.checkSum=cal_checkSum(send);
                    sendBuf=toCharStar(send);
                    sendByteCnt+=send.getByteLength();
                    sendByteCnt+=send.getByteLength();
                    sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
                    cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                        <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
                    delete sendBuf;
                }
                else
                    state=ESTABLISHED;
                break;
            }
            case CLOSE_WAIT:
            {
                Package send;
                send.seqNum=rand() % (int)pow(2,10);
                seqNum=send.seqNum;
                send.flags=0|FIN;
                send.checkSum=cal_checkSum(send);
                sendBuf=toCharStar(send);
                sendto(sockSrv,sendBuf,send.getByteLength(), 0,(SOCKADDR*) &addrClient, sizeof(addrClient));
                cout<<"[LOG]: "<<"SEND "<<"seq:"<<send.seqNum<<"\t"<<"ack:"<<send.ackNum<<"\t"
                    <<"flags:"<<(int)send.flags<<"\t"<<"checksum:"<<send.checkSum<<"\t"<<"dataLength:"<<send.dataLength<<endl;
                delete sendBuf;
                state = LAST_ACK;
                break;
            }
            case LAST_ACK:
            {
                recvfrom(sockSrv,recvBuf, bufLen, 0, (SOCKADDR *) & addrClient, &addrClientSize);
                cout<<"[LOG]: "<<"RECV "<<"seq:"<<getSeqNum()<<"\t"<<"ack:"<<getAckNum()<<"\t"
                    <<"flags:"<<(int)getFlag()<<"\t"<<"checksum:"<<getCheckSum()<<"\t"<<"dataLength:"<<getDataLength()<<endl;
                if((int(getFlag())|0)==0b0010 && cal_checkSum()==getCheckSum())  //收到ACK
                {
                    state=CLOSED;
                }
                else state=LAST_ACK;
            }
        }
    }
    cout<<"[LOG:] connection dismissed."<<endl;
    QueryPerformanceCounter((LARGE_INTEGER*)&tail );
    cout<<sendByteCnt/(( tail-head) * 1000.0 / freq) <<"Byte/ms"<<endl;
    cout<<( tail-head) * 1000.0 / freq <<"ms"<<endl;
    cout<<"Bye"<<endl;
    //system("pause");
    return 0;
}
