/*
负载均衡（Load Balance）其意思就是分摊到多个操作单元上进行执行，
例如Web服务器、FTP服务器、企业关键应用服务器和其它关键任务服务器等，
从而共同完成工作任务。
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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

#include "log.h"
#include "conn.h"
#include "mgr.h"
#include "warp.h"
#include "processpool.h"

using std::vector;

static const char* version = "1.0";

static void usage( const char* prog )
{
    log( LOG_INFO, __FILE__, __LINE__,  "使用: %s [-h] [-v] [-f config_file]", prog );
}

int main( int argc, char* argv[] )
{
    char cfg_file[1024];
    memset( cfg_file, '\0', 100 );
    int option;
    /*
        getopt函数这个函数可以通过提取命令行参数对命令行参数进行解析。
        如果选项成功找到，返回选项字母；如果所有命令行选项都解析完毕，返回 -1；
        如果遇到选项字符不在 optstring 中，返回字符 '?'；如果遇到丢失参数，
        那么返回值依赖于 optstring 中第一个字符，如果第一个字符是 ':' 则返回':'，
        否则返回'?'并提示出错误信息。
        optarg —— 指向当前选项参数(如果有)的指针。
        optind —— 再次调用 getopt() 时的下一个 argv指针的索引。
        optopt —— 最后一个未知选项。
        opterr ­—— 如果不希望getopt()打印出错信息，则只要将全域变量opterr设为0即可。
    */
    while ( ( option = getopt( argc, argv, "f:xvh" ) ) != -1 )
    {
        switch ( option )
        {
            case 'x':
            {
                set_loglevel( LOG_DEBUG );
                break;
            }
            case 'v':
            {
                log( LOG_INFO, __FILE__, __LINE__, "%s %s", argv[0], version );
                return 0;
            }
            case 'h':
            {
                usage( basename( argv[ 0 ] ) ); //path为".","/", ".."或者为不带有/的字符串时， 输出与path一致，最后一个 字符为/，输出为空;否则返回的是最后/后面的字符串
                return 0;
            }
            case 'f':
            {
                memcpy( cfg_file, optarg, strlen( optarg ) );
                break;
            }
            case '?':
            {
                //__FILE__:用以指示本行语句所在源文件的文件名
                //__LINE__:用以指示本行语句在源文件中的位置信息
                log( LOG_ERR, __FILE__, __LINE__, "无法验证！ %c", option );
                usage( basename( argv[ 0 ] ) );
                return 1;
            }
        }
    }    

    if( cfg_file[0] == '\0' )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "请指定配置文件！" );
        return 1;
    }
    int cfg_fd = open( cfg_file, O_RDONLY ); //只读方式打开
    if( !cfg_fd )
    {
        log( LOG_ERR, __FILE__, __LINE__, "读取配置文件遇到错误: %s", strerror( errno ) );
        return 1;
    }
    //struct stat这个结构体是用来描述一个linux系统文件系统中的文件属性的结构。
    //函数说明 fstat()用来将参数fildes所指的文件状态，复制到参数buf所指的结构中(struct stat)。
    //int fstat(int fildes,struct stat *buf)
    //下面主要为了获取ret_stat.st_size
    struct stat ret_stat;   
    if( fstat( cfg_fd, &ret_stat ) < 0 ) 
    {
        log( LOG_ERR, __FILE__, __LINE__, "读取配置文件遇到错误: %s", strerror( errno ) );
        return 1;
    }
    char* buf = new char [ret_stat.st_size + 1];
    memset( buf, '\0', ret_stat.st_size + 1 );
    ssize_t read_sz = read( cfg_fd, buf, ret_stat.st_size );
    if ( read_sz < 0 )
    {
        log( LOG_ERR, __FILE__, __LINE__, "读取配置文件遇到错误: %s", strerror( errno ) );
        return 1;
    }
    //host在前面的mgr.h文件中定义   //一个是负载均衡服务器,另一个是逻辑服务器
    vector< host > balance_srv; //负载均衡服务器
    vector< host > logical_srv; //逻辑服务器
    host tmp_host;
    memset( tmp_host.m_hostname, '\0', 1024 );//作用是将某一块内存中的内容全部设置为指定的值， 这个函数通常为新申请的内存做初始化工作。
    char* tmp_hostname;
    char* tmp_port;
    char* tmp_conncnt;
    bool opentag = false;
    char* tmp = buf; //此时tem指向config.xml文件的内容
    char* tmp2 = NULL;
    char* tmp3 = NULL;
    char* tmp4 = NULL;
    //在源字符串tmp中找出最先含有搜索字符串"\n"中任一字符的位置并返回，若没找到则返回空指针
    //strpbrk是在源字符串（s1）中找出最先含有搜索字符串（s2）中任一字符的位置并返回，若找不到则返回空指针。        
    while( tmp2 = strpbrk( tmp, "\n" ) )
    {
        *tmp2++ = '\0';
        //strstr(str1,str2) 函数用于判断字符串str2是否是str1的子串。如果是，则该函数返回 
        //str1字符串从 str2第一次出现的位置开始到 str1结尾的字符串；否则，返回NULL。
        if( strstr( tmp, "<logical_host>" ) )
        {
            if( opentag )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "分析配置文件失败！" ); 
                return 1; 
            }
            opentag = true;
        }
        else if( strstr( tmp, "</logical_host>" ) )
        {
            if( !opentag )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "分析配置文件失败！" );
                return 1;
            }
            logical_srv.push_back( tmp_host );
            memset( tmp_host.m_hostname, '\0', 1024 );
            opentag = false;
        }
        else if( tmp3 = strstr( tmp, "<name>" ) )
        {
            tmp_hostname = tmp3 + 6; //将tmp_hostname指针指向<name>后面的IP地址的首个地址
            tmp4 = strstr( tmp_hostname, "</name>" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "分析配置文件失败！" );
                return 1;
            }
            *tmp4 = '\0';
            memcpy( tmp_host.m_hostname, tmp_hostname, strlen( tmp_hostname ) );
        }
        else if( tmp3 = strstr( tmp, "<port>" ) )
        {
            tmp_port = tmp3 + 6;
            tmp4 = strstr( tmp_port, "</port>" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "分析配置文件失败！" );
                return 1;
            }
            *tmp4 = '\0';
            tmp_host.m_port = atoi( tmp_port );
        }
        else if( tmp3 = strstr( tmp, "<conns>" ) )
        {
            tmp_conncnt = tmp3 + 7;
            tmp4 = strstr( tmp_conncnt, "</conns>" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "分析配置文件失败！" );
                return 1;
            }
            *tmp4 = '\0';
            tmp_host.m_conncnt = atoi( tmp_conncnt );
        }
        else if( tmp3 = strstr( tmp, "Listen" ) )
        {
            tmp_hostname = tmp3 + 6;
            tmp4 = strstr( tmp_hostname, ":" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "分析配置文件失败!" );
                return 1;
            }
            *tmp4++ = '\0';
            tmp_host.m_port = atoi( tmp4 ); //atoi是把字符串转换成整型数的一个函数
            memcpy( tmp_host.m_hostname, tmp3, strlen( tmp3 ) );
            balance_srv.push_back( tmp_host );
            memset( tmp_host.m_hostname, '\0', 1024 );
        }
        tmp = tmp2;
    }

    if( balance_srv.size() == 0 || logical_srv.size() == 0 )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "分析配置文件失败!" );
        return 1;
    }
    const char* ip = balance_srv[0].m_hostname;
    int port = balance_srv[0].m_port;

    int listenfd = Socket( PF_INET, SOCK_STREAM, 0 ); //AF_INET和PF_INET的值是相同的
    //assert( Listenfd >= 0 ); //assert的作用是现计算表达式 expression ，如果其值为假（即为0），那么它先向stderr打印一条出错信息，然后通过调用 abort 来终止程序运行
 
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );//可以在将IP地址在“点分十进制”和“二进制整数”之间转换，而且inet_pton和inet_ntop这2个函数能够处理ipv4和ipv6。算是比较新的函数了。
    address.sin_port = htons( port );

    ret = Bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    //assert( ret != -1 );

    ret = listen( listenfd, 5 );
    //assert( ret != -1 );

    //memset( cfg_host.m_hostname, '\0', 1024 );
    //memcpy( cfg_host.m_hostname, "127.0.0.1", strlen( "127.0.0.1" ) );
    //cfg_host.m_port = 54321;
    //cfg_host.m_conncnt = 5;
    processpool< conn, host, mgr >* pool = processpool< conn, host, mgr >::create( listenfd, logical_srv.size() );
    if( pool )
    {
        pool->run( logical_srv );
        delete pool;
    }

    Close( listenfd );
    return 0;
}
