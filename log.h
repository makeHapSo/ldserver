#ifndef LOG_H
#define LOG_H

#include <syslog.h>
#include <cstdarg>

/*
    日志级别(包含在syslog.h头文件中)
    LOG_EMERG	紧急情况
    LOG_ALERT	应该被立即改正的问题，如系统数据库破坏
    LOG_CRIT	重要情况，如硬盘错误
    LOG_ERR	错误
    LOG_WARNING	警告信息
    LOG_NOTICE	不是错误情况，但是可能需要处理
    LOG_INFO	情报信息
    LOG_DEBUG	包含情报的信息，通常旨在调试一个程序时使用
*/

void set_loglevel( int log_level = LOG_DEBUG ); //设置日志等级
void log( int log_level, const char* file_name, int line_num, const char* format, ... ); //打印日志

#endif
