# hSrv
使用 socket 和 I/O多路复用(epoll) 实现的一个简单的 webServer，可以解析目录和常见格式的文件。

### Usage
```shell
# Clone the repository.
git clone https://github.com/zeureka/hSrv.git

# Compile
g++ main.c server.c -o server

# Run
./server 9001 /home/iaee/ 
```

