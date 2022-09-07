#pragma once

#include<exception>
#include<pthread.h>
#include<semaphore.h>

class Sem
{
    public:
        Sem()
        {
            if(sem_init(&sem_,0,0) !=0 )
            {
               throw std::exception();
            }
        }
        ~Sem()
        {
            sem_destroy(&sem_);
        }
        bool wait()
        {
            return sem_wait(&sem_) == 0;
        }
        bool post()
        {
            return sem_post(&sem_) == 0;
        }
    private:
        sem_t sem_;
};

class Locker
{
    public:
        Locker()
        {
            if(pthread_mutex_init(&mutex_,NULL)!= 0)
            {
                throw std::exception();
            }
        }
        ~Locker()
        {
            pthread_mutex_destroy(&mutex_);
        }
        //加锁
        bool lock()
        {
            return pthread_mutex_lock(&mutex_) == 0;
        }
        //解锁
        bool unlock()
        {
            return pthread_mutex_unlock(&mutex_) == 0;
        }
    private:
        pthread_mutex_t mutex_;
};

class Cond
{
    public:
        Cond()
        {
            if(pthread_mutex_init(&mutex_,NULL)!=0)
            {
                throw std::exception();
            }
            if( pthread_cond_init(&cond_,NULL)!=0)
            {
                pthread_mutex_destroy(&mutex_);
                throw std::exception();
            }
        }
        ~Cond()
        {
            pthread_mutex_destroy(&mutex_);
            pthread_cond_destroy(&cond_);
        }
        bool wait()
        {
            int ret =0 ;
            pthread_mutex_lock(&mutex_);
            ret = pthread_cond_wait(&cond_,&mutex_);
            pthread_mutex_unlock(&mutex_);
            return ret == 0;
        }
        bool signal()
        {
            return pthread_cond_signal(&cond_);
        }
    private:
        pthread_mutex_t mutex_;
        pthread_cond_t cond_;
};
