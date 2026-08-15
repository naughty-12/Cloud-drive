#ifndef INET_H
#define INET_H



#include <winsock2.h>
class INet{

public:
    INet()
    {

    }
    virtual ~INet()
    {

    }
public:
    virtual bool initNetWork(const char *szip="127.0.0.1",short nport=8899)=0;
    virtual void uninitNetWork(const char* szerr="")=0;
    virtual void recvData()=0;
    virtual bool sendData(const char*szbuf,int nlen)=0;
};
#endif // INET_H
