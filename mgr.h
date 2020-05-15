#ifndef SRVMGR_H
#define SRVMGR_H

#include <map>
#include <arpa/inet.h>
#include "fdwrapper.h"
#include "conn.h"

using std::map;

//服务器类，保存连接信息，包含在mgr类中
class host
{
public:
    char m_hostname[1024]; //ip地址
    int m_port; //端口号
    int m_conncnt; //连接数
};

//这个类为进程池类中的负责调度连接的类。
class mgr
{
public:
    mgr( int epollfd, const host& srv );//在构造mgr的同时调用conn2srv和服务端建立连接
    ~mgr();
    int conn2srv( const sockaddr_in& address );//和服务端建立连接同时返回socket描述符
    conn* pick_conn( int sockfd );//从连接好的连接中（m_conn中）拿出一个放入任务队列（m_used）中
    void free_conn( conn* connection ); // 释放连接（当连接关闭或者中断后，将其fd从内核事件表删除，并关闭fd），并将同srv进行连接的放入m_freed中
    int get_used_conn_cnt();//获取当前任务数(被notify_parent_busy_ratio()调用）
    void recycle_conns();//从m_freed中回收连接（由于连接已经被关闭，因此还要调用conn2srv()）放到m_conn中
    RET_CODE process( int fd, OP_TYPE type );//通过fd和type来控制对服务端和客户端的读写，是整个负载均衡的核心功能

private:
    static int m_epollfd;//内核时间表fd
    map< int, conn* > m_used;//要被使用的连接
    map< int, conn* > m_conns;//连接好的连接
    map< int, conn* > m_freed;//使用后被释放的连接
    host m_logic_srv;//保存服务端的信息
};

#endif
