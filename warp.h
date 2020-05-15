#ifndef WRAP_H
#define WRAP_h

void perr_exit(const char *s); //错误处理
int Accept(int fd, struct sockaddr *sa, socklen_t *salenptr); //接收错误处理
int Bind(int fd, const struct sockaddr *sa, socklen_t salen); //绑定
int Listen(int fd, int backlog); //设置最大链接个数
int Socket(int family, int type, int protocol); //套接字
int Close(int fd); //关闭套接字
int Connect(int fd, const struct sockaddr *sa, socklen_t salen); //链接

#endif
