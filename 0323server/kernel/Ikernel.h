#ifndef IKERNEL_H
#define IKERNEL_H
#include <winsock2.h>

class Ikernel{
public:
    Ikernel()
    {


             }
    virtual ~Ikernel()
    {

    }
public:
    virtual bool  boolopen()=0;
    virtual void close()=0;

    virtual bool dealData(SOCKET sock,const char*szbuf,int nlen)=0;
};


#endif // IKERNEL_H
