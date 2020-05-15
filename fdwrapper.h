#ifndef FDWRAPPER_H
#define FDWRAPPER_H

/*
LT模式时，事件就绪时，假设对事件没做处理，内核会反复通知事件就绪
ET模式时，事件就绪时，假设对事件没做处理，内核不会反复通知事件就绪
*/
//NOTHING 没有；IOERR io错误；CLOSED 关闭； BUFFER_FULL 缓冲区满了；BUFFER_EMPTY 缓冲区空； TRY_AGAIN 重新尝试
enum RET_CODE { OK = 0, NOTHING = 1, IOERR = -1, CLOSED = -2, BUFFER_FULL = -3, BUFFER_EMPTY = -4, TRY_AGAIN };//枚举返回信息
enum OP_TYPE { READ = 0, WRITE, ERROR };//读、写、错误
int setnonblocking( int fd );//设置非阻塞
void add_read_fd( int epollfd, int fd );//增加读事件到epfd中
void add_write_fd( int epollfd, int fd );//增加写事件到epfd中
void removefd( int epollfd, int fd );//从epfd中删除一个fd
void closefd( int epollfd, int fd );//关闭epoll
void modfd( int epollfd, int fd, int ev );//修改已经注册的fd的监听事件

#endif
