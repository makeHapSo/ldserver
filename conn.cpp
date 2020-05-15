#include <exception>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "conn.h"
#include "log.h"
#include "fdwrapper.h"

//初始化客户端和服务端的缓冲区
conn::conn()
{
    m_srvfd = -1;
    m_clt_buf = new char[ BUF_SIZE ];
    if( !m_clt_buf )
    {
        perror("new erro");
        //throw std::exception();//它只报告异常的发生，不提供任何额外的信息
    }
    m_srv_buf = new char[ BUF_SIZE ];
    if( !m_srv_buf )
    {
        perror("new erro");
        //throw std::exception();
    }
    reset();
}

//析构函数回收客户端和服务端的缓冲区
conn::~conn()
{
    delete [] m_clt_buf;
    delete [] m_srv_buf;
}

//初始化客户端地址
void conn::init_clt( int sockfd, const sockaddr_in& client_addr )
{
    m_cltfd = sockfd;
    m_clt_address = client_addr;
}

//初始化服务器端地址
void conn::init_srv( int sockfd, const sockaddr_in& server_addr )
{
    m_srvfd = sockfd;
    m_srv_address = server_addr;
}

//重置读写缓冲
void conn::reset()
{
    m_clt_read_idx = 0;
    m_clt_write_idx = 0;
    m_srv_read_idx = 0;
    m_srv_write_idx = 0;
    m_srv_closed = false;
    m_cltfd = -1;
    memset( m_clt_buf, '\0', BUF_SIZE );
    memset( m_srv_buf, '\0', BUF_SIZE );
}

//从客户端读入的信息写入m_clt_buf
RET_CODE conn::read_clt()
{
    int bytes_read = 0;
    while( true )
    {
        if( m_clt_read_idx >= BUF_SIZE )//如果读入的数据大于BUF_SIZE     
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "客户端读缓冲区满！, 让服务器端写！" );
            return BUFFER_FULL;
        }
        //因为存在分包的问题（recv所读入的并非是size的大小），
        //因此我们根据recv的返回值进行循环读入，直到读满m_clt_buf或者recv的返回值为0（对端的socket已正常关闭）
        //recv()接收对端的数据
        bytes_read = recv( m_cltfd, m_clt_buf + m_clt_read_idx, BUF_SIZE - m_clt_read_idx, 0 );
        if ( bytes_read == -1 )
        {
            //非阻塞情况下： EAGAIN表示没有数据可读，请尝试再次调用,而在阻塞情况下，
            //如果被中断，则返回EINTR;  EWOULDBLOCK等同于EAGAIN
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                break;
            }
            return IOERR;
        }
        else if ( bytes_read == 0 )//连接被关闭
        {
            return CLOSED;
        }

        m_clt_read_idx += bytes_read;//移动读下标
    }
    return ( ( m_clt_read_idx - m_clt_write_idx ) > 0 ) ? OK : NOTHING;//当读下标大于写下标时代表正常
}

//从服务端读入的信息写入m_srv_buf
RET_CODE conn::read_srv()
{
    int bytes_read = 0;
    while( true )
    {
        if( m_srv_read_idx >= BUF_SIZE )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "服务器端缓冲区满！, 让客户端写！" );
            return BUFFER_FULL;
        }

        bytes_read = recv( m_srvfd, m_srv_buf + m_srv_read_idx, BUF_SIZE - m_srv_read_idx, 0 );
        if ( bytes_read == -1 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                break;
            }
            return IOERR;
        }
        else if ( bytes_read == 0 )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "服务器不应关闭持久连接！" );
            return CLOSED;
        }

        m_srv_read_idx += bytes_read;
    }
    return ( ( m_srv_read_idx - m_srv_write_idx ) > 0 ) ? OK : NOTHING;
}

//把从客户端读入m_clt_buf的内容写入服务端
RET_CODE conn::write_srv()
{
    int bytes_write = 0;
    while( true )
    {
        //如果客户端读下标小于等于客户端写的下标(先读后写)
        if( m_clt_read_idx <= m_clt_write_idx )
        {
            m_clt_read_idx = 0;
            m_clt_write_idx = 0;
            return BUFFER_EMPTY;
        }
        //send()是一个计算机函数，功能是向一个已经连接的socket发送数据
        bytes_write = send( m_srvfd, m_clt_buf + m_clt_write_idx, m_clt_read_idx - m_clt_write_idx, 0 );
        if ( bytes_write == -1 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                return TRY_AGAIN;
            }
            log( LOG_ERR, __FILE__, __LINE__, "服务器端写套接字关闭！, %s", strerror( errno ) );
            return IOERR;
        }
        else if ( bytes_write == 0 )
        {
            return CLOSED;
        }

        m_clt_write_idx += bytes_write;
    }
}

//把从服务端读入m_srv_buf的内容写入客户端
RET_CODE conn::write_clt()
{
    int bytes_write = 0;
    while( true )
    {
        if( m_srv_read_idx <= m_srv_write_idx )
        {
            m_srv_read_idx = 0;
            m_srv_write_idx = 0;
            return BUFFER_EMPTY;
        }

        bytes_write = send( m_cltfd, m_srv_buf + m_srv_write_idx, m_srv_read_idx - m_srv_write_idx, 0 );
        if ( bytes_write == -1 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                return TRY_AGAIN;
            }
            log( LOG_ERR, __FILE__, __LINE__, "客户端写套接字关闭, %s", strerror( errno ) );
            return IOERR;
        }
        else if ( bytes_write == 0 )
        {
            return CLOSED;
        }

        m_srv_write_idx += bytes_write;
    }
}
