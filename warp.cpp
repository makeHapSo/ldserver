#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

#include "warp.h"

void perr_exit(const char *s)
{
	perror(s);
	exit(-1);
}

int Connect(int fd, const struct sockaddr *sa, socklen_t salen)
{
    int n;

	if ((n = connect(fd, sa, salen)) < 0)
		perr_exit("connect error");

    return n;
}

int Accept(int fd, struct sockaddr *sa, socklen_t *salenptr)
{
	int n;
	/*ECONNABORTED描述为“software caused connection abort”，
    即“软件引起的连接中止”。原因在于当服务和客户进程在完成
    用于 TCP 连接的“三次握手”后，客户 TCP 
    却发送了一个 RST （复位）分节，在服务进程看来，
    就在该连接已由 TCP 排队
    等着服务进程调用 accept 的时候 RST 却到达了*/
    /*如果进程在一个慢系统调用(slow system call)中阻塞时，
    当捕获到某个信号且相应信号处理函数返回时，这个系
    统调用被中断，调用返回错误，设置errno为EINTR
    */
again:
	if ((n = accept(fd, sa, salenptr)) < 0) {
		if ((errno == ECONNABORTED) || (errno == EINTR))
			goto again;
		else
			perr_exit("accept error");
	}
	return n;
}

int Bind(int fd, const struct sockaddr *sa, socklen_t salen)
{
    int n;

	if ((n = bind(fd, sa, salen)) < 0)
		perr_exit("bind error");

    return n;
}

int Listen(int fd, int backlog)
{
    int n;

	if ((n = listen(fd, backlog)) < 0)
		perr_exit("listen error");

    return n;
}

int Socket(int family, int type, int protocol)
{
	int n;

	if ((n = socket(family, type, protocol)) < 0)
		perr_exit("socket error");

	return n;
}

int Close(int fd)
{
    int n;
	if ((n = close(fd)) == -1)
		perr_exit("close error");

    return n;
}