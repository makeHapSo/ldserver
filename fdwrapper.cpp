#ifndef FDWRAPPER_H
#define FDWRAPPER_H

#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include "warp.h"

//设置非阻塞
int setnonblocking( int fd )
{
    /*
    fcntl是计算机中的一种函数，通过fcntl可以改变已打开的文件性质
    F_GETFL 取得文件描述符状态旗标，此旗标为open（）的参数flags。
    F_SETFL 设置文件描述符状态旗标
    */
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

//增加读事件到epfd中
void add_read_fd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;//EPOLLIN表示对应的文件描述符可以读（包括对端SOCKET正常关闭）；；fd设置为ET模式(边缘触发)
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

//增加写事件到epfd中
void add_write_fd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLOUT | EPOLLET;//表示对应的文件描述符可以写；
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

//关闭epoll
void closefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    Close( fd );
}

//从epfd中删除一个fd
void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
}

//修改已经注册的fd的监听事件
void modfd( int epollfd, int fd, int ev )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

#endif
