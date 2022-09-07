BUILD_DIR=./build
CC=g++
CFLAGS=-Wall  -c
OBJS= $(BUILD_DIR)/Logger.o $(BUILD_DIR)/HttpConn.o  \
$(BUILD_DIR)/Timestamp.o  $(BUILD_DIR)/main.o

###------c代码编译------------
$(BUILD_DIR)/Logger.o : Logger.cc Logger.h Timestamp.h 
	$(CC) $(CFLAGS) $< -o $@ -g

$(BUILD_DIR)/HttpConn.o :  HttpConn.cc Locker.h HttpConn.h Logger.h
	$(CC) $(CFLAGS) $< -o $@ -g 

$(BUILD_DIR)/Timestamp.o : Timestamp.cc Timestamp.h
	$(CC) $(CFLAGS) $< -o $@ -g

$(BUILD_DIR)/main.o : main.cc Logger.h Locker.h  ThreadPool.h HttpConn.h TimeHeap.h
	$(CC) $(CFLAGS) $< -o $@ -g

## 目标文件
$(BUILD_DIR)/webserver : $(OBJS) 
	$(CC) $^ -o $@ -g

.PHONY:mk_dir clean all 

mk_dir: 
	if [[!-d $(BUILD_DIR)]];then mkdir $(BUILD_DIR);fi
clean:  
	cd $(BUILD_DIR) && rm -f ./* 
build: $(BUILD_DIR)/webserver

all: mk_dir build