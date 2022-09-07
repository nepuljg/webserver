#pragma once 
#include<iostream>
#include<vector>
#include<unordered_map>
#include "Logger.h"
#include "Timestamp.h"

class HeapTimer;//定时器类，前向声明
//绑定socket和定时器
struct ClientData
{
    sockaddr_in address_;
    int sockfd_;
    HeapTimer *timer_;
};

//定时器类
class HeapTimer
{
public:
    HeapTimer():expire_(time(NULL)){};

    time_t expire_;
    void (*cb_fun)(ClientData*);
    ClientData *userdata_;

    bool operator<(const HeapTimer*t)
    {
        return expire_<t->expire_;
    }
};
//时间堆类
class TimeHeap
{
public:
    ~TimeHeap()
    {
        while(heap_.size()>1)
        {
            auto tmp=heap_.back();
            if(tmp)delete tmp;
            heap_.pop_back();
        }
        map_.clear();
        size_=0;
    }
    TimeHeap():size_(0)
    {
        heap_.push_back(NULL);//第一个元素不使用
    }
   
    //插入定时器
    void addTimer(HeapTimer*timer)
    {
         if(!timer)return;
         heap_.push_back(timer);
         size_++;
         map_[timer]=size_;
         down(size_);
         up(size_);
    }
    //删除指定定时器
    void delTimer(HeapTimer*timer)
    {
         if(!timer)return;
        // std::cout<<timer<<std::endl;
         if(!map_.count(timer))
         {
            LOG_FATAL("%s\n","del timer error! no this timer");
         }
         int idx=map_[timer];
         map_.erase(timer);
         heap_[idx]=heap_[size_--];
         heap_.pop_back();
         up(idx);
         down(idx);
    }
    //调整定时器
    void addJust(HeapTimer*timer,time_t timeout)
    {
         if(!map_.count(timer))
         {
            LOG_FATAL("%s\n","addJust timer error! no this timer");
         }
         timer->expire_=timeout;
         int idx=map_[timer];
         down(idx);
         up(idx);
         
    }
    bool empty() const 
    {
         return size_==0;
    }
    //int size() const {return size_-1;}
    //心搏函数
    time_t  tick()
    {
         if(empty())
         {
            LOG_ERROR("%s\n","TimeHeap is empty!");
         }
         HeapTimer*timer=heap_[1];
         time_t cur=time(NULL);
         
         while(!empty())
         {
              if(timer->expire_>cur)
              {
                break;
              }
              if(timer->cb_fun)
              {
                 timer->cb_fun(timer->userdata_);
              }
              delTimer(timer);
              if(!empty())timer=heap_[1];
         }
         if(!empty())
         {
            return heap_[1]->expire_-cur;
         }
         return 0;
    }
    int size() const {return size_;}
private:
    std::vector<HeapTimer*>heap_;
    std::unordered_map<HeapTimer*,int>map_;
    int size_;

     //donw操作，把
    void down(int k)
    {
        //看k是否比
         int t=k;
         if(k*2<=size_&&heap_[k*2]<heap_[t])t=k*2;
         if(k*2+1<=size_&&heap_[k*2+1]<heap_[t])t=k*2+1;
         
         if(t!=k)
         {
            std::swap(map_[heap_[k]],map_[heap_[t]]);
            std::swap(heap_[k],heap_[t]);
            down(t);
         }
    }
    void up(int k)
    {
         while(k/2&&heap_[k/2]<heap_[k])
         {
            std::swap(map_[heap_[k/2]],map_[heap_[k]]);
            std::swap(heap_[k/2],heap_[k]);
            k=k/2;
         }
    }
};