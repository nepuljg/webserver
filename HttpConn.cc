#include "HttpConn.h"
class HttpConn;

const char*ok_200_title = "OK";
const char*error_400_title="Bad Request";
const char*error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char*error_403_title="Forbidden";
const char*error_403_form="You do not have permission to get file  from this server.\n";
const char*error_404_title="Not Found";
const char*error_404_form="The requested file was not found on this server.\n";
const char*error_500_title="Internal Error";
const char*error_500_form= "There was an unusual problem serving the requested file.\n";
const char*docRoot="/home/return/webserver/resources";

int setnonblocking(int fd)
{
    int oldOption_=fcntl(fd,F_GETFL);
    int newOption_=oldOption_|O_NONBLOCK;
    fcntl(fd,F_SETFL,newOption_);
    return oldOption_;
}

void addfd(int epollfd,int fd,bool one_shot)
{
     epoll_event event;
     event.data.fd=fd;
     event.events=EPOLLIN|EPOLLRDHUP;
     if(one_shot)
     {
        event.events|=EPOLLONESHOT;
     }
     epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
     setnonblocking(fd);
}
void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}
void modfd(int epollfd,int fd,int ev)
{
     epoll_event event;
     event.data.fd=fd;
     event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
     epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

int HttpConn::userCount_ =0;
int HttpConn::epollfd_=-1;

void HttpConn::closeConn(bool realClose)
{
     LOG_DEBUG("closeConn\n");
     if(realClose &&(sockfd_!=-1))
     {
        removefd(epollfd_,sockfd_);
        sockfd_=-1;
        userCount_--;
     }
}
void HttpConn::init(int sockfd,const sockaddr_in&addr)
{
    LOG_DEBUG("init(int fd,const sockaddr_in)\n");
    sockfd_ = sockfd;
    address_ = addr;
    int reuse =1;
    setsockopt(sockfd_,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(epollfd_,sockfd_,true);
    userCount_++;

    init();
    
}

void HttpConn::init()
{
    LOG_DEBUG("init()\n");
    bytesToSend_=0;
    bytesHaveSend_=0;

    checkState_=CHECK_STATE_REQUESTLINE;
    linger_=false;
    
    method_=GET;
    url_=0;
    version_=0;
    contentLength_=0;
    host_=0;
    startLine_=0;
    checkedIdx_=0;
    writeIdx_=0;
    readIdx_=0;

    memset(readBuf,'\0',READ_BUFFER_SIZE);
    memset(writeBuf,'\0',WRITE_BUFFER_SIZE);
    memset(realFile,'\0',FILENAME_LEN);
}
//解析行，对于\r\n代表行的结尾
HttpConn::LINE_STATUS HttpConn::parseLine()
{
    LOG_DEBUG("parseLine()\n");
    char tmp;
    for(;checkedIdx_<readIdx_;++checkedIdx_)
    {
        tmp=readBuf[checkedIdx_];
        if(tmp == '\r')
        {
            if((checkedIdx_+1)== readIdx_)
            {
                return LINE_OPEN;
            }
            else if(readBuf[checkedIdx_+1]=='\n')
            {
                readBuf[checkedIdx_++]='\0';
                readBuf[checkedIdx_++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(tmp=='\n')
        {
            if((checkedIdx_>1)&& (readBuf[checkedIdx_-1]=='\r'))
            {
                readBuf[checkedIdx_-1]='\0';
                readBuf[checkedIdx_++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

bool HttpConn::read()
{
   // HeapTimer*timer=clientdata_[sockfd_].timer_;//得到对应的定时器
    LOG_DEBUG("read()\n");
    if(readIdx_>=READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytesRead_=0;
    
    while(true)
    {
        bytesRead_=recv(sockfd_,readBuf+readIdx_,READ_BUFFER_SIZE-readIdx_,0);
        if( bytesRead_ == -1)
        {
            if(errno == EAGAIN || errno ==EWOULDBLOCK)
            {
                break;
            }
            return false;
        }
        else if(bytesRead_ == 0)
        {

            return false;
        }
        readIdx_+=bytesRead_;
    }
    LOG_INFO("read sucess\n");
    return true;
}

//GET URL  HTTP/1.1
//解析请求行
HttpConn::HTTP_CODE HttpConn::parseRequestLine(char*text)
{
    LOG_DEBUG("parseRequestLine\n");
    url_=strpbrk(text," \t");//找到第一个" \t"的位置
    //如果没有，就肯定出错了
    if(!url_)
    {
        return BAD_REQUEST;
    }
    *url_++='\0';
    char*method = text;
    if(strcasecmp(method,"GET")==0)
    {
        method_=GET;
    }
    else 
    {
        return BAD_REQUEST;
    }

    url_+=strspn(url_," \t");//取出多余的空格，找到第一个不是“ \t"的位置
    version_=strpbrk(url_," \t");//从后续的字符中找到" \t"的第一个位置
    if(!version_)
    {
        return BAD_REQUEST;
    }
    *version_++='\0';
    version_+=strspn(version_," \t");
    
    if(strcasecmp(version_,"HTTP/1.1")!=0)
    {
        return BAD_REQUEST;
    }

    if(strncasecmp(url_,"http://",7)==0)
    {
        url_+=7;
        url_=strchr(url_,'/');
    }
    if(!url_||url_[0]!='/')
    {
        return BAD_REQUEST;
    }
    checkState_=CHECK_STATE_HEADER;
    LOG_INFO("pareseRequestLine sucess\n");
    return NO_REQUEST;
}
//解析头部
HttpConn::HTTP_CODE HttpConn::parseHeaders(char*text)
{
    LOG_DEBUG("parseHeaders\n");
    if(text[0]=='\0'){
        if(method_==HEAD)
        {
            return GET_REQUEST;
        }
        if(contentLength_!=0)
        {
            checkState_=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text,"Connection:",11)==0)
    {
        text+=11;
        text+=strspn(text," \t");
        if(strcasecmp(text,"Keep-alive")==0)
        {
            linger_=true;
        }
    }
    else if(strncasecmp(text,"Content-Length:",15)==0)
    {
        text+=15;
        text+=strspn(text," \t");
        contentLength_=atol(text);
    }
    else if(strncasecmp(text,"Host:",5)==0)
    {
        text+=5;
        text+=strspn(text," \t");
        host_=text;
    }
    else 
    {
        LOG_INFO("oop! unknow header %s\n",text);
    }
     LOG_INFO("pareseHeaders sucess\n");
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn:: parseContent(char*text)
{
    LOG_DEBUG("parseContent\n");
    if(readIdx_>=(contentLength_+checkedIdx_))
    {
        text[contentLength_]='\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}
HttpConn::HTTP_CODE HttpConn::processRead()
{
    LOG_DEBUG("processRead()\n");
    LINE_STATUS lineStatus_ =LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char*text =0;

    while(((checkState_==CHECK_STATE_CONTENT)&&(lineStatus_==LINE_OK))||
            ((lineStatus_=parseLine())==LINE_OK))
    {
        text=getLine();
        startLine_=checkedIdx_;
        LOG_INFO("got 1 http line:%s\n",text);
        
        switch(checkState_)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                 ret = parseRequestLine(text);
                 if(ret ==BAD_REQUEST)
                 {
                    LOG_DEBUG("%s:%d BAD_REQUEST\n",__FILE__,(int)__LINE__);
                    return BAD_REQUEST;
                 }
                 break;
            }
            case CHECK_STATE_HEADER:
            {
                 ret = parseHeaders(text);
                 if(ret == BAD_REQUEST)
                 {
                    return BAD_REQUEST;
                 }
                 else if(ret == GET_REQUEST)
                 {
                    return doRequest();
                 }
                 break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret =parseContent(text);
                if(ret ==GET_REQUEST)
                {
                    return doRequest();
                }
                lineStatus_=LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::doRequest()
{
    LOG_DEBUG("doRequest()\n");
    strcpy(realFile,docRoot);
    int len=strlen(docRoot);
    strncpy(realFile+len,url_,FILENAME_LEN-len-1);
    if(stat(realFile,&fileStat_)<0)
    {
        return NO_RESOURCE;
    }
    if(!(fileStat_.st_mode&S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    if(S_ISDIR(fileStat_.st_mode))
    {
        return BAD_REQUEST;
    }

    int fd=open(realFile,O_RDONLY);
    fileAddr_=(char*) mmap(0,fileStat_.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return FILE_REQUEST;
}

void HttpConn::unmap()
{
    LOG_DEBUG("unmap()\n");
    if(fileAddr_)
    {
        munmap(fileAddr_,fileStat_.st_size);
        fileAddr_=0;
    }
}
bool HttpConn::write()
{
    LOG_DEBUG("write()\n");
     int tmp =0;
     if(bytesToSend_==0)
     {
        modfd(epollfd_,sockfd_,EPOLLIN);
        init();
        return true;
     }
     
     while(true)
     {
        tmp=writev(sockfd_,iv,ivCount_);
        if(tmp<=-1)
        {
            if(errno ==EAGAIN)
            {
                modfd(epollfd_,sockfd_,EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytesHaveSend_+=tmp;
        bytesToSend_-=tmp;

        if(bytesHaveSend_>=(int)iv[0].iov_len)
        {
           iv[0].iov_len=0;
           iv[1].iov_base=fileAddr_+(bytesHaveSend_-writeIdx_);
           iv[1].iov_len=bytesToSend_;
        }
        else 
        {
            iv[0].iov_base=writeBuf+bytesHaveSend_;
            iv[0].iov_len=iv[0].iov_len-tmp;
        }

        if(bytesToSend_<=0)
        {
            //没有数据要发送了
            unmap();
            modfd(epollfd_,sockfd_,EPOLLIN);
            
            if(linger_)
            {
                init();
                return true;
            }
            else return false;
        }
     }
}

bool HttpConn::addResponse(const char*format,...)
{
    LOG_DEBUG("addResponse()\n");
     if(writeIdx_>=WRITE_BUFFER_SIZE)
     {
        return false;
     }
     va_list arg_list;
     va_start(arg_list,format);
     int len=vsnprintf(writeBuf+writeIdx_,WRITE_BUFFER_SIZE-1-writeIdx_,format,arg_list);
     if(len>=(WRITE_BUFFER_SIZE-1-writeIdx_))
     {
        return false;
     }
     writeIdx_+=len;
     va_end(arg_list);
     return true;
}

bool HttpConn::addStatusLine(int status,const char*title)
{
     LOG_DEBUG("addStatusLine\n");
     return addResponse("%s %d %s\r\n","HTTP/1.1",status,title);
}
bool HttpConn::addHeaders(int contentLength)
{
     LOG_DEBUG("addHeaders\n");
     addContentLength(contentLength);
     addLinger();
     addBlankLine();
     return true;
}
bool HttpConn::addContentLength(int contentLength)
{
     LOG_DEBUG("addContentLength\n");
     return addResponse("Content-Length: %d\r\n",contentLength);
}
bool HttpConn::addLinger()
{
     LOG_DEBUG("addLinger\n");
     return addResponse("Connection: %s\r\n",(linger_==true)?"Keep-alive":"close");
}
bool HttpConn::addBlankLine()
{
    LOG_DEBUG("addBlankLine()\n");
    return addResponse("%s","\r\n");
}
bool HttpConn::addContent(const char*content)
{
     LOG_DEBUG("addContent\n");
     return addResponse("%s",content);
}
bool HttpConn::processWrite(HTTP_CODE ret)
{
    LOG_DEBUG("processWrite\n");
     switch( ret )
     {
        case INTERNAL_ERROR:
        {
            addStatusLine(500,error_500_title);
            addHeaders(strlen(error_500_form));
            if(!addContent(error_500_form))
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            addStatusLine(400,error_400_title);
            addHeaders(strlen(error_400_form));
            if(!addContent(error_400_form))
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            addStatusLine(404,error_404_title);
            addHeaders(strlen(error_404_form));
            if(!addContent(error_404_form))
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            addStatusLine(403,error_403_title);
            addHeaders(strlen(error_403_form));
            if(!addContent(error_403_form))
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            addStatusLine(200,ok_200_title);
           // addHeaders(fileStat_.st_size);
            if(fileStat_.st_size!=0)
            {
                addHeaders(fileStat_.st_size);
                iv[0].iov_base = writeBuf;
                iv[0].iov_len=writeIdx_;
                iv[1].iov_base=fileAddr_;
                iv[1].iov_len=fileStat_.st_size;
                ivCount_=2;
                bytesToSend_=writeIdx_+fileStat_.st_size;
                return true;
            }
            else 
            {
                const char* okString = "<html><body></body></html>";
                addHeaders(strlen(okString));
                if(!addContent(okString))
                {
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
     }
     
     iv[0].iov_base=writeBuf;
     iv[0].iov_len=writeIdx_;
     bytesToSend_=writeIdx_;
     ivCount_=1;
     return true;
}

void HttpConn::process()
{
   // LOG_DEBUG("process\n");
   // LOG_INFO("start do this request\n");
   // if()
    HTTP_CODE readRet_ = processRead();
    
    if(readRet_ == NO_REQUEST)
    {
        LOG_DEBUG("%s:%d NO_REQUEST\n",__FILE__,(int)__LINE__);
        modfd(epollfd_,sockfd_,EPOLLIN);
        return;
    }
    bool writeRet_ = processWrite(readRet_);
    if(!writeRet_)
    {
        closeConn();
    }
    modfd(epollfd_,sockfd_,EPOLLOUT);
}