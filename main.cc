#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<sys/epoll.h>
#include<cassert>

#include "Locker.h"
#include "ThreadPool.h"
#include "HttpConn.h"
#include "Logger.h"
#include "TimeHeap.h"

#define TIMESLOT 5
#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

static int pipefd[2];
static TimeHeap timerHeap_;
int epollfd=0;

extern int addfd(int epollfd,int fd,bool oneShot);
extern int remove(int epollfd,int fd);
extern int setnonblocking(int fd);


void addsig(int sig,void(handler)(int),bool restart=true)
{
     struct sigaction sa;
     memset(&sa,'\0',sizeof(sa));
     sa.sa_handler=handler;
     if(restart)
     {
        sa.sa_flags|=SA_RESTART;
     }
     sigfillset(&sa.sa_mask);
     if(sigaction(sig,&sa,NULL)==-1)
     {
        LOG_FATAL(" %s:%s:%d\n",__FILE__,__FUNCTION__,(int)__LINE__);
     }
}
void showError(int connfd,const char*info)
{
     LOG_INFO("%s\n",info);
     send(connfd,info,strlen(info),0);
     close(connfd);
}

void sigHandler(int sig)
{
     int saveError=errno;
     int msg=sig;
     send(pipefd[1],(char*)&msg,1,0);
     errno=saveError;
}
void timerHandler()
{
     //定时处理任务，就是调用回调函数
     time_t timeslot=timerHeap_.tick();
     if(timeslot)alarm(timeslot);
     else 
        alarm(TIMESLOT);
}
//定时器回调函数,删除非活动链接socket上的注册事件，并关闭之
void cb_func(ClientData*userdata)
{
     epoll_ctl(epollfd,EPOLL_CTL_DEL,userdata->sockfd_,0);
     assert(userdata);
     close(userdata->sockfd_);
     HttpConn::userCount_--;
     LOG_INFO("close fd %d \n",userdata->sockfd_);
}
int main(int argc,char*argv[])
{
    if(argc<=2)
    {
        LOG_INFO("usage: %s ipAddress portNumer\n",basename(argv[0]));
        return 1;
    }
    const char*ip= argv[1];
    int port = atoi(argv[2]);
    
    addsig(SIGPIPE,SIG_IGN,true);
    ThreadPool <HttpConn> *pool =NULL;
    
    try 
    {
        pool =new ThreadPool<HttpConn>;
    }
    catch( ... )
    {
        LOG_FATAL("%s:%s:%d\n",__FILE__,__FUNCTION__,(int)__LINE__);
    }
    
    HttpConn *users_=new HttpConn[MAX_FD];
    assert(users_);
    
    int listenfd=socket(PF_INET,SOCK_STREAM,0);
    assert(listenfd>=0);
    
    // struct linger tmp={1,0};
    // setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    
    int ret=0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family=AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr);
    address.sin_port=htons(port);

    //端口复用
    int reuse=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    ret=bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    assert(ret>=0);

    ret=listen(listenfd,5);
    assert(ret>=0);

    epoll_event events[MAX_EVENT_NUMBER];
    epollfd=epoll_create(5);
    assert(epollfd!=-1);
    addfd(epollfd,listenfd,false);

    HttpConn::epollfd_=epollfd;
     // 设置管道
    ret=socketpair(PF_UNIX,SOCK_STREAM,0,pipefd);
    assert(ret!=-1);
    setnonblocking(pipefd[1]);
    addfd(epollfd,pipefd[0],false);
    
    //设置信号处理函数
    addsig(SIGALRM,sigHandler,false);
    addsig(SIGTERM,sigHandler,false);
    //先初始化用户,在TimeHeap文件中初始化
    ClientData*  clientdata_=new ClientData[MAX_FD];

    bool timeout=false;//首先设置false;
    alarm(TIMESLOT); //定时
    
    while(true)
    {
        int number = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number<0)&&(errno!=EINTR))
        {
            LOG_FATAL("%s\n","epoll failure");
        }

        for(int i=0;i<number;++i)
        {
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd)
            {
                struct sockaddr_in clientAddress_;
                socklen_t clientAddressLength_ = sizeof(clientAddress_);
                int connfd=accept(listenfd,(struct sockaddr*)&clientAddress_,&clientAddressLength_);
                if(connfd<0)
                {
                    LOG_FATAL("errno is:%d\n",errno);
                }
                if(HttpConn::userCount_>=MAX_FD)
                {
                    close(connfd);
                    showError(connfd,"Internal server busy");
                    continue;
                }
                LOG_INFO("The connection fd:%d,address:%s,port:%d\n",connfd,inet_ntoa(clientAddress_.sin_addr),clientAddress_.sin_port);
                users_[connfd].init(connfd,clientAddress_);
                
                clientdata_[connfd].address_=clientAddress_;
                clientdata_[connfd].sockfd_=connfd;
                //创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到时间堆容器中
                HeapTimer*timer=new HeapTimer;
                timer->userdata_=&clientdata_[connfd];
                timer->cb_fun=cb_func;
                timer->expire_=time(NULL)+3*TIMESLOT;
                clientdata_[connfd].timer_=timer;
                timerHeap_.addTimer(timer);
                LOG_INFO("timerHeap size %d \n",(int)timerHeap_.size());
                // //test
                // users_timer[connfd].address = clientAddress_;
                // users_timer[connfd].sockfd = connfd;
                // util_timer *timer = new util_timer;
                // timer->user_data = &users_timer[connfd];
                // timer->cb_func = cb_fun;
                // time_t cur = time(NULL);
                // timer->expire = cur + 3 * TIMESLOT;
                // users_timer[connfd].timer = timer;
                // timer_lst.add_timer(timer);
            }
            else if(events[i].events& (EPOLLRDHUP|EPOLLHUP|EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                HeapTimer *timer=clientdata_[sockfd].timer_;
                if(timer)
                {
                    timer->cb_fun(&clientdata_[sockfd]);
                    timerHeap_.delTimer(timer);
                }
                // util_timer *timer = users_timer[sockfd].timer;
                // timer->cb_func(&users_timer[sockfd]);

                // if (timer)
                // {
                //     timer_lst.del_timer(timer);
                // }
                
                // //LOG_INFO("to closeConn()");
                users_[sockfd].closeConn();
            }
            else if(events[i].events&EPOLLIN)
            {
                //处理信号
                if(sockfd==pipefd[0])
                {
                    char signals[1024]={0};
                    ret=recv(pipefd[0],signals,sizeof(signals),0);
                    if(ret==-1)
                    {
                        continue;
                    }
                    else if(ret ==0 )
                    {
                         continue;
                    }
                    else 
                    {
                        for(int i=0;i<ret;++i)
                        {
                            switch(signals[i])
                            {
                                case SIGALRM:
                                {
                                    LOG_INFO("%s","time out !");
                                    //用timeout标记定时任务需要处理，但不立即处理定时任务，优先处理其他重要的任务
                                    timeout=true;
                                    break;
                                }
                                case SIGTERM:
                                {
                                     return 0;
                                }
                            }
                        }
                    }
                    continue;
                }
                HeapTimer *timer=clientdata_[sockfd].timer_;
              //  util_timer *timer = users_timer[sockfd].timer;
                if(users_[sockfd].read())
                {
                   // LOG_INFO("pool->append()");
                    pool->append(users_+sockfd);
                    //若有数据传输，则定时器往后延迟3个单位
                    
                    if(timer)
                    {
                        LOG_INFO("%s","adjust time once!");
                        time_t cur=time(NULL);
                        timerHeap_.addJust(timer,cur+3*TIMESLOT);//调整一下
                        // time_t cur = time(NULL);
                        // timer->expire = cur + 3 * TIMESLOT;
                        // LOG_INFO("%s", "adjust timer once");
                        // //Log::get_instance()->flush();
                        // timer_lst.adjust_timer(timer);
                    }
                }
                else 
                {
                    // LOG_INFO("to closeConn()");
                    if(timer)
                    {
                        LOG_INFO("%s","call cb_fun,del timer !");
                        timer->cb_fun(&clientdata_[sockfd]);
                        timerHeap_.delTimer(timer);
                    }
                    // timer->cb_func(&users_timer[sockfd]);
                    // if (timer)
                    // {
                    //     timer_lst.del_timer(timer);
                    // }
                    users_[sockfd].closeConn();
                }
            }else if(events[i].events&EPOLLOUT)
            {
                HeapTimer *timer=clientdata_[sockfd].timer_;
                if(users_[sockfd].write())
                {
                    if (timer)
                    {
                        LOG_INFO("%s","adjust time once!");
                        time_t cur=time(NULL);
                        timerHeap_.addJust(timer,cur+3*TIMESLOT);//调整一下
                    }
                    
                }
                else 
                {
                    if(timer)
                    {
                        LOG_INFO("%s","call cb_fun,del timer!");
                        timer->cb_fun(&clientdata_[sockfd]);
                        timerHeap_.delTimer(timer);
                    }
                    users_[sockfd].closeConn();
                }
            }
            
        }
        if(timeout)
        {
            timerHandler();
            timeout=false;
        }
    }

    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete [] users_;
    delete [] clientdata_;
    delete pool;
    return 0;
}