#pragma once 

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>

#include "Locker.h"
#include "Logger.h"
#include "noncopyable.h"

template<typename T>
class ThreadPool : public noncopyable
{
public:
    ThreadPool(int threadNumber=8,int maxRequests = 10000);
    ~ThreadPool();
    bool append(T*request);
private:
    static void *worker(void*arg);
    void run();
private:
    int threadNumber_;
    int maxRequests_;
    pthread_t *threads_;
    std::list<T*>workQueue_;
    Locker queueLocker_;
    Sem queueStat_;
    bool stop_;
};

template<typename T>
ThreadPool<T> ::ThreadPool(int threadNumber,int maxRequests)
:threadNumber_(threadNumber)
,maxRequests_(maxRequests)
,stop_(false)
,threads_(NULL)
{
    if((threadNumber_<=0) ||(maxRequests_<=0))
    {
        LOG_FATAL("%s:%s:%d error\n",__FILE__,__FUNCTION__,(int)__LINE__);
    }
    threads_ = new pthread_t[threadNumber_];
    if(!threads_)
    {
        LOG_FATAL("%s:%s:%d error\n",__FILE__,__FUNCTION__,(int)__LINE__);
    }
    
    for(int i=0;i<threadNumber_;++i)
    {
        LOG_INFO("create the %dth thread\n",i);
        if(pthread_create(threads_+i,NULL,worker,this)!=0)
        {
            delete [] threads_;
            LOG_FATAL("%s:%s:%d error\n",__FILE__,__FUNCTION__,(int)__LINE__);
        }
        //设置脱离线程
        if(pthread_detach(threads_[i]))
        {
            delete [] threads_;
            LOG_FATAL("%s:%s:%d error\n",__FILE__,__FUNCTION__,(int)__LINE__);
        }
    }
}

template<typename T>
ThreadPool<T> ::~ThreadPool()
{
    delete [] threads_;
    stop_ = true;
}

template<typename T>
bool ThreadPool<T> :: append(T*request)
{
    queueLocker_.lock();
    if((int)workQueue_.size()>maxRequests_)
    {
        queueLocker_.unlock();
        return false;
    }
    workQueue_.push_back(request);
    queueLocker_.unlock();
    queueStat_.post();
    return true;
}
template<typename T>
void *ThreadPool<T> ::worker(void*arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    pool->run();
    return pool;
}
template<typename T>
void ThreadPool<T>::run()
{
     while(!stop_)
     {
      //  LOG_INFO("queeueState_.wait()\n");
        queueStat_.wait();  //起到阻塞作用
        queueLocker_.lock();
        
        if(workQueue_.empty())
        {
            queueLocker_.unlock();
            continue;
        }
        T*request = workQueue_.front();
        workQueue_.pop_front();
        queueLocker_.unlock();
        
        if(!request)
        {
            continue;
        }
        request->process();  //HttpConn类中的
     }
}