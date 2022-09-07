# webserver
# Linux下轻量级web服务器
##使用线程池+非阻塞socket+epoll+事件处理(模拟Proactor实现)的并发模型
## 使用有限自动机解析HTTP请求报文，支持解析GET请求
## 使用时间堆定时器来管理关闭非活动连接
## 基于单例模式实现同步日志系统
## 经Webbench压力测试可以实现上万的并发连接数据交换
# 运行
直接执行``./run.sh``即可，其中端口为``12345``
# 效果
[!image](https://github.com/nepuljg/webserver/blob/main/text.png)

