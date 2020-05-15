#ifndef CONN_H
#define CONN_H

#include <arpa/inet.h>
#include "fdwrapper.h"

//这个类主要负责连接好之后对客户端和服务端的读写操作，以及返回服务端的状态
class conn
{
public:
    conn();//初始化客户端和服务端的缓冲区
    ~conn();//析构函数回收客户端和服务端的缓冲区
    void init_clt( int sockfd, const sockaddr_in& client_addr );//初始化客户端地址
    void init_srv( int sockfd, const sockaddr_in& server_addr );//初始化服务器端地址
    void reset();//重置读写缓冲
    RET_CODE read_clt();//从客户端读入的信息写入m_clt_buf
    RET_CODE write_clt();//把从服务端读入m_srv_buf的内容写入客户端
    RET_CODE read_srv();//从服务端读入的信息写入m_srv_buf
    RET_CODE write_srv();//把从客户端读入m_clt_buf的内容写入服务端

public:
    static const int BUF_SIZE = 2048; //缓冲区大小

    char* m_clt_buf;//客户端文件缓冲区
    int m_clt_read_idx;//客户端读下标
    int m_clt_write_idx;//客户端写下标
    sockaddr_in m_clt_address;//客户端地址
    int m_cltfd;//客户端fd

    char* m_srv_buf; //服务端文件缓冲区
    int m_srv_read_idx;//服务端读下标
    int m_srv_write_idx;//服务端写下标
    sockaddr_in m_srv_address;//服务端地址
    int m_srvfd;//服务端fd

    bool m_srv_closed;//标志（用来标志服务端是否关闭）
};

#endif
