#pragma once 

#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/uio.h>

#include "Locker.h"
#include "Logger.h"
#include "TimeHeap.h"

class HttpConn
{
    public:
        static const int FILENAME_LEN= 200;
        static const int READ_BUFFER_SIZE = 2048;
        static const int WRITE_BUFFER_SIZE = 1024;

        enum METHOD
        {
            GET=0,
            POST,
            HEAD
        };
        enum CHECK_STATE
        {
            CHECK_STATE_REQUESTLINE=0,
            CHECK_STATE_HEADER,
            CHECK_STATE_CONTENT
        };
        enum HTTP_CODE
        {
            NO_REQUEST,
            GET_REQUEST,
            BAD_REQUEST,
            NO_RESOURCE,
            FORBIDDEN_REQUEST,
            FILE_REQUEST,
            INTERNAL_ERROR,
            CLOSED_CONNECTION
        };
        enum LINE_STATUS
        {
          LINE_OK =0,
          LINE_BAD,
          LINE_OPEN  
        };
    public:
        HttpConn(){};
        ~HttpConn(){};
    public:
        void init(int sockfd,const sockaddr_in &addr);
        void closeConn(bool realClose = true);
        void process();
        bool read();
        bool write();
    
    private:
        void init();
        HTTP_CODE processRead();
        bool processWrite(HTTP_CODE ret);
        
        HTTP_CODE parseRequestLine(char*text);
        HTTP_CODE parseHeaders(char*text);
        HTTP_CODE parseContent(char*text);
        HTTP_CODE doRequest();

        char*getLine(){return readBuf+startLine_;}
        LINE_STATUS parseLine();
        
        void unmap();
        bool addResponse(const char*format,...);
        bool addContent(const char*content);
        bool addContentType();
        bool addStatusLine(int status,const char*title);
        bool addHeaders(int contentLength);
        bool addContentLength(int contentLength);
        bool addLinger();
        bool addBlankLine();

    public:
        static int epollfd_;
        static int userCount_;
    private:
        int sockfd_;
        sockaddr_in address_;

        char readBuf[READ_BUFFER_SIZE];
        int readIdx_;
        int checkedIdx_;
        int startLine_;
        
        char writeBuf[WRITE_BUFFER_SIZE];
        int writeIdx_;

        CHECK_STATE checkState_;
        METHOD method_;

        char realFile[FILENAME_LEN];
        char*url_;
        char*version_;
        char*host_;
        int contentLength_;
        bool linger_;

        char*fileAddr_;
        struct stat fileStat_;
        struct iovec iv[2];
        int ivCount_;

        int bytesToSend_; //将要发送的字节数
        int bytesHaveSend_; //已经发送的字节数

};