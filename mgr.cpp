#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <exception>
#include "log.h"
#include "mgr.h"
#include "warp.h"

/*
    pair将一对值(T1和T2)组合成一个值
    两个值可以分别用pair的两个公有函数first和second访问
*/
using std::pair;

int mgr::m_epollfd = -1;

//和服务端建立连接同时返回socket描述符
int mgr::conn2srv( const sockaddr_in& address )
{
    int sockfd = Socket( PF_INET, SOCK_STREAM, 0 );
    //if( sockfd < 0 )
    //{
    //   return -1;
    //}
    
    //if ( Connect( sockfd, ( struct sockaddr* )&address, sizeof( address ) ) != 0  )
    //{
    //    Close( sockfd );
    //    return -1;
    //}
    Connect(sockfd, (struct sockaddr*)&address, sizeof(address));
    return sockfd;
}

//在构造mgr的同时调用conn2srv和服务端建立连接
mgr::mgr( int epollfd, const host& srv ) : m_logic_srv( srv )
{
    m_epollfd = epollfd;
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, srv.m_hostname, &address.sin_addr );
    address.sin_port = htons( srv.m_port );
    log( LOG_INFO, __FILE__, __LINE__, "逻辑服务器信息: (%s, %d)", srv.m_hostname, srv.m_port );

    for( int i = 0; i < srv.m_conncnt; ++i )
    {
        sleep( 1 );
        int sockfd = conn2srv( address );
        if( sockfd < 0 )
        {
            log( LOG_ERR, __FILE__, __LINE__, "建立连接 %d 失败！", i );
        }
        else
        {
            log( LOG_INFO, __FILE__, __LINE__, "建立连接 %d 到服务器成功！", i );
            conn* tmp = NULL;
            try
            {
                tmp = new conn;
            }
            catch( ... )
            {
                Close( sockfd );
                continue;
            }
            tmp->init_srv( sockfd, address );
            m_conns.insert( pair< int, conn* >( sockfd, tmp ) );
        }
    }
}

mgr::~mgr()
{
}

//获取当前任务数(被notify_parent_busy_ratio()调用）
int mgr::get_used_conn_cnt()
{
    return m_used.size();
}

//从连接好的连接中（m_conns中）拿出一个放入任务队列（m_used）中
conn* mgr::pick_conn( int cltfd  )
{
    if( m_conns.empty() )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "没有足够的连接到服务器！" );
        return NULL;
    }

    map< int, conn* >::iterator iter =  m_conns.begin();
    int srvfd = iter->first;
    conn* tmp = iter->second;
    if( !tmp )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "服务器连接对象为空！" );
        return NULL;
    }
    m_conns.erase( iter );
    m_used.insert( pair< int, conn* >( cltfd, tmp ) );
    m_used.insert( pair< int, conn* >( srvfd, tmp ) );
    add_read_fd( m_epollfd, cltfd );
    add_read_fd( m_epollfd, srvfd );
    log( LOG_INFO, __FILE__, __LINE__, "绑定客户端套接字 %d 和服务端套接字 %d", cltfd, srvfd );
    return tmp;
}

// 释放连接（当连接关闭或者中断后，将其fd从内核事件表删除，并关闭fd），并将同srv进行连接的放入m_freed中
void mgr::free_conn( conn* connection )
{
    int cltfd = connection->m_cltfd;
    int srvfd = connection->m_srvfd;
    closefd( m_epollfd, cltfd );
    closefd( m_epollfd, srvfd );
    m_used.erase( cltfd );
    m_used.erase( srvfd );
    connection->reset();
    m_freed.insert( pair< int, conn* >( srvfd, connection ) );
}

//从m_freed中回收连接（由于连接已经被关闭，因此还要调用conn2srv()）放到m_conn中
void mgr::recycle_conns()
{
    if( m_freed.empty() )
    {
        return;
    }
    for( map< int, conn* >::iterator iter = m_freed.begin(); iter != m_freed.end(); iter++ )
    {
        sleep( 1 );
        int srvfd = iter->first;
        conn* tmp = iter->second;
        srvfd = conn2srv( tmp->m_srv_address );
        if( srvfd < 0 )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "修复连接失败！");
        }
        else
        {
            log( LOG_INFO, __FILE__, __LINE__, "%s", "修复连接成功！" );
            tmp->init_srv( srvfd, tmp->m_srv_address );
            m_conns.insert( pair< int, conn* >( srvfd, tmp ) );
        }
    }
    m_freed.clear();
}

//通过fd和type来控制对服务端和客户端的读写，是整个负载均衡的核心功能
RET_CODE mgr::process( int fd, OP_TYPE type )
{
    //根据fd找到conn*指针
    conn* connection = m_used[ fd ];//首先根据fd获取连接类，该类中保存有相对应的客户端和服务端的fd
    if( !connection )
    {
        return NOTHING;
    }
    if( connection->m_cltfd == fd )//如果是客户端fd
    {
        int srvfd = connection->m_srvfd;
        switch( type )
        {
            case READ://读操作
            {
                RET_CODE res = connection->read_clt();//则调用conn的read_ckt方法
                switch( res )
                {
                    case OK:
                    {
                        log( LOG_DEBUG, __FILE__, __LINE__, "从客户端读取的内容: %s", connection->m_clt_buf );
                    }
                    case BUFFER_FULL:
                    {
                        modfd( m_epollfd, srvfd, EPOLLOUT );
                        break;
                    }
                    case IOERR:
                    case CLOSED://客户端关闭连接
                    {
                        free_conn( connection );
                        return CLOSED;
                    }
                    default:
                        break;
                }
                if( connection->m_srv_closed )//服务端关闭连接
                {
                    free_conn( connection );
                    return CLOSED;
                }
                break;
            }
            case WRITE:
            {
                RET_CODE res = connection->write_clt();
                switch( res )
                {
                    case TRY_AGAIN:
                    {
                        modfd( m_epollfd, fd, EPOLLOUT );
                        break;
                    }
                    case BUFFER_EMPTY:
                    {
                        modfd( m_epollfd, srvfd, EPOLLIN );
                        modfd( m_epollfd, fd, EPOLLIN );
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
                        free_conn( connection );
                        return CLOSED;
                    }
                    default:
                        break;
                }
                if( connection->m_srv_closed )
                {
                    free_conn( connection );
                    return CLOSED;
                }
                break;
            }
            default:
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "其他操作尚不支持" );
                break;
            }
        }
    }
    else if( connection->m_srvfd == fd )//如果是服务端fd
    {
        int cltfd = connection->m_cltfd;
        switch( type )
        {
            case READ:
            {
                RET_CODE res = connection->read_srv();
                switch( res )
                {
                    case OK:
                    {
                        //此处的break不能加，在读完消息之后
                        //应该继续去触发BUFFER_FULL从而通知可写
                        log( LOG_DEBUG, __FILE__, __LINE__, "从服务器读取的内容: %s", connection->m_srv_buf );
                    }
                    case BUFFER_FULL:
                    {
                        modfd( m_epollfd, cltfd, EPOLLOUT );
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
                        modfd( m_epollfd, cltfd, EPOLLOUT );
                        connection->m_srv_closed = true;
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case WRITE:
            {
                RET_CODE res = connection->write_srv();
                switch( res )
                {
                    case TRY_AGAIN:
                    {
                        modfd( m_epollfd, fd, EPOLLOUT );
                        break;
                    }
                    case BUFFER_EMPTY:
                    {
                        modfd( m_epollfd, cltfd, EPOLLIN );
                        modfd( m_epollfd, fd, EPOLLIN );
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
                        modfd( m_epollfd, cltfd, EPOLLOUT );
                        connection->m_srv_closed = true;
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            default:
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "其他操作尚不支持" );
                break;
            }
        }
    }
    else
    {
        return NOTHING;
    }
    return OK;
}
