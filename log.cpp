#include <stdio.h>
#include <time.h>
#include <string.h>
#include "log.h"

static int level = LOG_INFO;
static int LOG_BUFFER_SIZE = 2048;
static const char* loglevels[] =
{
    "emerge!", "alert!", "critical!", "error!", "warn!", "notice:", "info:", "debug:"
};

void set_loglevel( int log_level )
{
    level = log_level;
}

void log( int log_level,  const char* file_name, int line_num, const char* format, ... )
{
    if ( log_level > level )
    {
        return;
    }

    time_t tmp = time( NULL ); //time(NULL)函数还回当前的时间。1970年元旦午夜0点到现在的秒数
    struct tm* cur_time = localtime( &tmp ); //我们可通过tm结构来获得日期和时间
    if ( ! cur_time )
    {
        return;
    }

    char arg_buffer[ LOG_BUFFER_SIZE ];
    memset( arg_buffer, '\0', LOG_BUFFER_SIZE );
    strftime( arg_buffer, LOG_BUFFER_SIZE - 1, "[ %x %X ] ", cur_time ); //根据区域设置格式化本地时间/日期，函数的功能将时间格式化，或者说格式化一个时间字符串。
    printf( "%s", arg_buffer );
    printf( "%s:%04d ", file_name, line_num );
    printf( "%s ", loglevels[ log_level - LOG_EMERG ] );

    va_list arg_list; //是在C语言中解决变参问题的一组宏，所在头文件：#include <stdarg.h>，用于获取不确定个数的参数。
    va_start( arg_list, format );
    memset( arg_buffer, '\0', LOG_BUFFER_SIZE );
    vsnprintf( arg_buffer, LOG_BUFFER_SIZE - 1, format, arg_list ); //用于向字符串中打印数据、数据格式用户自定义
    printf( "%s\n", arg_buffer );
    fflush( stdout ); //清除读写缓冲区，在需要立即把输出缓冲区的数据进行物理写入时，stdout这个表达式指向一个与标准输出流（standard output stream）相关连的 FILE 对象。
    va_end( arg_list ); //置空arg_list
}
