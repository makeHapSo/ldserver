# ldserver
负载均衡服务器
参考《Linux高性能服务器》游双著

# 下载说明
下载方法：git clone https://github.com/985911541/ldserver.git

# 使用说明
在clone的目录下直接make,得到可执行文件

![img1](https://github.com/985911541/ldserver/blob/master/pic/1.PNG)

通过nsloookup www.tencent.com 命令查看腾讯官网的ip地址

![img2](https://github.com/985911541/ldserver/blob/master/pic/2.PNG)

设置config.conf
其中Listen 后面的ip和端口是本负载均衡服务器的ip和端口号
<logic_host></logic_host>之间的是逻辑服务器的ip、端口号、连接数

![img3](https://github.com/985911541/ldserver/blob/master/pic/3.PNG)

命令./springsnail -f config.conf启动服务器
可以看到有2个5次连接，因为我们设置了两个www.tencent.com 的ip地址每个地址有5个连接，这显然是正确的。

![img4](https://github.com/985911541/ldserver/blob/master/pic/4.PNG)

通过telnet 127.0.0.1 50000连接服务器，得到反馈

![img5](https://github.com/985911541/ldserver/blob/master/pic/5.PNG)

![img6](https://github.com/985911541/ldserver/blob/master/pic/6.PNG)

在多试几次，也是正确无疑的。

![img7](https://github.com/985911541/ldserver/blob/master/pic/7.PNG)

![img8](https://github.com/985911541/ldserver/blob/master/pic/8.PNG)

# 代码说明
各个文件的作用
main.cpp:
1.设置命令行参数的处理
2.读取配置文件
3.解析配置文件
4.开始进程池的循环

processpool: 进程池，主要函数run(),run_child(),run_parent()

fdwrapper：操作fd的函数(增加读、写事件到epoll对象等)

log：连接读写，和返回信息

conn：连接读写操作，和返回相关信息

mgr：处理网络连接和负载均衡的框架(调度连接)

warp：socket相关错误处理封装函数

# 整体说明
简简单单画了下大概的架构图

![img9](https://github.com/985911541/ldserver/blob/master/pic/9.PNG)

我在代码中的注释是非常精细的，一些不常用的函数都有注释，免去大家去百度的时间。
当然如何发现这个项目有错误的地方欢迎大家指正。

qq：985911541
