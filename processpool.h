#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

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
#include <vector>

#include "warp.h"
#include "log.h"
#include "fdwrapper.h"

using std::vector;
/*
    void assert( int expression );
    assert的作用是先计算表达式 expression ，如果其值为假（即为0），
    那么它先向stderr打印一条出错信息，然后通过调用 abort 来终止程序运行
*/
//进程类，用来保存子进程的一些信息，在processpool使用
class process
{
public:
    process() : m_pid( -1 ){}

public:
    int m_busy_ratio;   //给每台实际处理服务器（业务逻辑服务器）分配一个加权比例
    pid_t m_pid;        //目标子进程的PID 
    int m_pipefd[2];    //父进程和子进程通信用的管道,父进程给子进程通知事件，子进程给父进程发送加权比
};

//进程池类，内部有整体的一个框架，通过这个类将其他类整合到一起
template< typename C, typename H, typename M >
class processpool
{
private:
    processpool( int listenfd, int process_number = 8 ); //单例
public:
    static processpool< C, H, M >* create( int listenfd, int process_number = 8 )
    {
        if( !m_instance ) //单例模式
        {
            m_instance = new processpool< C, H, M >( listenfd, process_number );
        }
        return m_instance;
    }
    ~processpool()
    {
        delete [] m_sub_process;
        //delete m_instance;
    }
    void run( const vector<H>& arg ); //启动进程池

private:
    void notify_parent_busy_ratio( int pipefd, M* manager );//获取目前连接数量，将其发送给父进程
    int get_most_free_srv();//找出最空闲的服务器
    void setup_sig_pipe();//统一事件源
    void run_parent();
    void run_child( const vector<H>& arg );

private:
    static const int MAX_PROCESS_NUMBER = 16;//进程池允许最大进程数量
    static const int USER_PER_PROCESS = 65536;//每个子进程最多能处理的客户数量
    static const int MAX_EVENT_NUMBER = 10000;//EPOLL最多能处理的的事件数
    int m_process_number;//进程池中的进程总数
    int m_idx;//子进程在池中的序号（从0开始）
    int m_epollfd;//当前进程的epoll内核事件表fd
    int m_listenfd;//监听socket
    int m_stop;//子进程通过m_stop来决定是否停止运行
    process* m_sub_process;//保存所有子进程的描述信息
    static processpool< C, H, M >* m_instance;//进程池静态实例
};

template< typename C, typename H, typename M >
processpool< C, H, M >* processpool< C, H, M >::m_instance = NULL;

static int EPOLL_WAIT_TIME = 5000;//eppll的超时值
static int sig_pipefd[2];//用于处理信号的管道，以实现统一事件源,后面称之为信号管道
static void sig_handler( int sig )//信号处理函数，将捕获的信号通过sig_pipefd发送给调用的进程
{
    int save_errno = errno;
    int msg = sig;
    send( sig_pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

static void addsig( int sig, void(* handler )(int), bool restart = true )//捕捉信号函数
{
    /*
    信号是异步的，它会在程序的任何地方发生。由此程序正常的执行路径被打破，
    去执行信号处理函数。一般情况下，进程正在执行某个系统调用，那么在该系统调用返回前信号是不会被递送的。
    但慢速系统调用除外，如读写终端、网络、磁盘，以及wait和pause。这些系统调用都会返回-1，errno置为EINTR。
    当系统调用被中断时，我们可以选择使用循环再次调用，或者设置重新启动该系统调用(SA_RESTART)。
    */
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if( restart )
    {
        //SA_RESTART用在为某个信号设置信号处理函数时，给该信号设置的一个标记。
        sa.sa_flags |= SA_RESTART;//设置重新启动该系统调用
    }
    sigfillset( &sa.sa_mask );//sigfillset()用来将参数set信号集初始化，然后把所有的信号加入到此信号集里即将所有的信号标志位置为1，屏蔽所有的信号。
    assert( sigaction( sig, &sa, NULL ) != -1 );//检查或修改与指定信号相关联的处理动作
}

template< typename C, typename H, typename M >
void processpool< C, H, M >::setup_sig_pipe()
{
    m_epollfd = epoll_create( 5 );
    assert( m_epollfd != -1 );

    int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, sig_pipefd );   //全双工管道
    assert( ret != -1 );

    setnonblocking( sig_pipefd[1] );  //设置非阻塞
    add_read_fd( m_epollfd, sig_pipefd[0] ); //增加读事件到epfd中

    addsig( SIGCHLD, sig_handler );  //子进程状态发生变化（退出或暂停）
    addsig( SIGTERM, sig_handler );  //终止进程,kill命令默认发送的即为SIGTERM
    addsig( SIGINT, sig_handler );   //键盘输入中断进程（Ctrl + C）
    addsig( SIGPIPE, SIG_IGN );      /*往被关闭的文件描述符中写数据时触发会使程序退出
                                       SIG_IGN可以忽略，在write的时候返回-1,
                                       errno设置为SIGPIPE*/
}

template< typename C, typename H, typename M >
processpool< C, H, M >::processpool( int listenfd, int process_number ) 
    : m_listenfd( listenfd ), m_process_number( process_number ), m_idx( -1 ), m_stop( false )
{
    assert( ( process_number > 0 ) && ( process_number <= MAX_PROCESS_NUMBER ) );

    m_sub_process = new process[ process_number ];
    assert( m_sub_process );

    for( int i = 0; i < process_number; ++i )
    {
        //socketpair()函数用于创建一对无名的、相互连接的套接子。 
        //如果函数成功，则返回0，创建好的套接字分别是sv[0]和sv[1]；否则返回-1，错误码保存于errno中。
        int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd );//建立一对匿名的已经连接的套接字放入m_pipefd
        assert( ret == 0 );

        m_sub_process[i].m_pid = fork();
        assert( m_sub_process[i].m_pid >= 0 );
        if( m_sub_process[i].m_pid > 0 ) //父进程
        {
            Close( m_sub_process[i].m_pipefd[1] );
            m_sub_process[i].m_busy_ratio = 0;
            continue;
        }
        else//子进程
        {
            Close( m_sub_process[i].m_pipefd[0] );
            m_idx = i;
            break;
        }
    }
}

template< typename C, typename H, typename M >
int processpool< C, H, M >::get_most_free_srv()//获取空闲的连接
{
    int ratio = m_sub_process[0].m_busy_ratio;
    int idx = 0;
    for( int i = 0; i < m_process_number; ++i )
    {
        if( m_sub_process[i].m_busy_ratio < ratio )
        {
            idx = i;
            ratio = m_sub_process[i].m_busy_ratio;
        }
    }
    return idx;
}

template< typename C, typename H, typename M >
void processpool< C, H, M >::run( const vector<H>& arg )
{
    if( m_idx != -1 )
    {
        run_child( arg );
        return;
    }
    run_parent();
}

//获取目前连接数量，将其发送给父进程
template< typename C, typename H, typename M >
void processpool< C, H, M >::notify_parent_busy_ratio( int pipefd, M* manager )
{
    int msg = manager->get_used_conn_cnt();
    send( pipefd, ( char* )&msg, 1, 0 );    
}

template< typename C, typename H, typename M >
void processpool< C, H, M >::run_child( const vector<H>& arg )
{
    setup_sig_pipe();//注册统一事件源

    int pipefd_read = m_sub_process[m_idx].m_pipefd[ 1 ];
    add_read_fd( m_epollfd, pipefd_read );

    epoll_event events[ MAX_EVENT_NUMBER ];

    M* manager = new M( m_epollfd, arg[m_idx] );//此处实例化一个mgr类的对象
    assert( manager );

    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIME );//监听m_epollfd上是否有事件
        if ( ( number < 0 ) && ( errno != EINTR ) )//错误处理
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "epoll 失败！" );
            break;
        }

        if( number == 0 )//在EPOLL_WAIT_TIME指定事件内没有事件到达时返回0
        {
            manager->recycle_conns();//从m_freed中回收连接
            continue;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            if( ( sockfd == pipefd_read ) && ( events[i].events & EPOLLIN ) ) //是父进程发送的消息（通知有新的客户连接到来）
            {
                int client = 0;
                ret = recv( sockfd, ( char* )&client, sizeof( client ), 0 ); 
                if( ( ( ret < 0 ) && ( errno != EAGAIN ) ) || ret == 0 ) //recv失败或者errno != EAGAIN
                {
                    continue;
                }
                else//建立和客户端的连接
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof( client_address );
                    int connfd = Accept( m_listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                    add_read_fd( m_epollfd, connfd );//将客户端文件描述符connfd上的可读事件加入内核时间表
                    C* conn = manager->pick_conn( connfd );//获取一个空闲的连接
                    if( !conn )
                    {
                        closefd( m_epollfd, connfd );
                        continue;
                    }
                    conn->init_clt( connfd, client_address );//初始化客户端信息
                    notify_parent_busy_ratio( pipefd_read, manager );
                }
            }
            //处理自身进程接收到的信号
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                //waitpid函数作用同于wait，但可指定pid进程清理，可以不阻塞
                                //成功：返回清理掉的子进程ID；失败：-1（无子进程）
                                //WNOHANG 若pid指定的子进程没有结束，则waitpid()函数返回0，不予以等待。若结束，则返回该子进程的ID。
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )//等收集退出的子进程，由于设置了WNOHANG因此不等待
                                {
                                    continue;
                                }
                                break;
                            }
                            case SIGTERM://退出该进程
                            case SIGINT:
                            {
                                m_stop = true;
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else if( events[i].events & EPOLLIN )//有sockfd上有数据可读
            {
                 RET_CODE result = manager->process( sockfd, READ );
                 switch( result )
                 {
                     case CLOSED:
                     {
                         notify_parent_busy_ratio( pipefd_read, manager );
                         break;
                     }
                     default:
                         break;
                 }
            }
            else if( events[i].events & EPOLLOUT )//有事件可写（只有sockfd写缓冲满了或者给某个sockfd注册O_EPOLLOUT才会触发）
            {
                 RET_CODE result = manager->process( sockfd, WRITE );
                 switch( result )//根据返回的状态进行处理
                 {
                     case CLOSED:
                     {
                         notify_parent_busy_ratio( pipefd_read, manager );
                         break;
                     }
                     default:
                         break;
                 }
            }
            else
            {
                continue;
            }
        }
    }

    Close( pipefd_read );
    Close( m_epollfd );
}

template< typename C, typename H, typename M >
void processpool< C, H, M >::run_parent()
{
    setup_sig_pipe();//注册统一事件源

    for( int i = 0; i < m_process_number; ++i )
    {
        add_read_fd( m_epollfd, m_sub_process[i].m_pipefd[ 0 ] );
    }

    add_read_fd( m_epollfd, m_listenfd );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIME );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "epoll 失败！" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            if( sockfd == m_listenfd )//来自监听套接字的事件(有连接进来)
            {
                /*
                int i =  sub_process_counter;
                do
                {
                    if( m_sub_process[i].m_pid != -1 )
                    {
                        break;
                    }
                    i = (i+1)%m_process_number;
                }
                while( i != sub_process_counter );
                
                if( m_sub_process[i].m_pid == -1 )
                {
                    m_stop = true;
                    break;
                }
                sub_process_counter = (i+1)%m_process_number;
                */
                int idx = get_most_free_srv();//获取空闲的连接（该连接在run->child()内，初始化mgr的时候已经创建好）
                send( m_sub_process[idx].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );//发送给子进程获取一个连接
                log( LOG_INFO, __FILE__, __LINE__, "向子进程发送请求 %d", idx );
            }
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )//处理自身进程接收到的信号
            {
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )//回收子进程
                                {
                                    for( int i = 0; i < m_process_number; ++i )
                                    {
                                        if( m_sub_process[i].m_pid == pid )
                                        {
                                            log( LOG_INFO, __FILE__, __LINE__, "child %d join", i );
                                            Close( m_sub_process[i].m_pipefd[0] );
                                            m_sub_process[i].m_pid = -1;
                                        }
                                    }
                                }
                                m_stop = true;
                                for( int i = 0; i < m_process_number; ++i )
                                {
                                    if( m_sub_process[i].m_pid != -1 )
                                    {
                                        m_stop = false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                log( LOG_INFO, __FILE__, __LINE__, "%s", "杀死所有子进程" );
                                for( int i = 0; i < m_process_number; ++i )
                                {
                                    int pid = m_sub_process[i].m_pid;
                                    if( pid != -1 )
                                    {
                                        kill( pid, SIGTERM );
                                    }
                                }
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else if( events[i].events & EPOLLIN )//有sockfd上有数据可读
            {
                int busy_ratio = 0;
                ret = recv( sockfd, ( char* )&busy_ratio, sizeof( busy_ratio ), 0 );
                if( ( ( ret < 0 ) && ( errno != EAGAIN ) ) || ret == 0 )
                {
                    continue;
                }
                for( int i = 0; i < m_process_number; ++i )
                {
                    if( sockfd == m_sub_process[i].m_pipefd[0] )
                    {
                        m_sub_process[i].m_busy_ratio = busy_ratio;
                        break;
                    }
                }
                continue;
            }
        }
    }

    for( int i = 0; i < m_process_number; ++i )
    {
        closefd( m_epollfd, m_sub_process[i].m_pipefd[ 0 ] );
    }
    Close( m_epollfd );
}

#endif
