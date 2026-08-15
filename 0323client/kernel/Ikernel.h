#ifndef IKERNEL_H
#define IKERNEL_H

class Ikernel{
public:
    Ikernel()
    {

    }
    virtual ~Ikernel()
    {

    }
public:
    virtual bool  connectServer(const char*szip="127.0.0.1",short nport=8899)=0;
    virtual void disconnectServer(const char*szerr="")=0;
    virtual bool sendData(const char*szbuf,int nlen)=0;
    virtual void dealdata(const char*szbuf, int nlen)=0;
};






#endif // IKERNEL_H
