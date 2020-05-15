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



