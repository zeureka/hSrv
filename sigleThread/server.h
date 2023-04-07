#ifndef __HTTP_SERVER_VERSION1
#define __HTTP_SERVER_VERSION1

// 初始化监听套接字
int initListenFd(unsigned short port);

// 启动 epoll
int epollRun(int lfd);

// 和客户端建立连接的函数
int acceptClient(int epfd, int lfd);

// 接收 http 请求
int recvHttpRequest(int epfd, int cfd);

// 解析 http 请求协议行
// 请求行对应的字符串 用于通信的文件描述符
int parseRequestLine(const char *line, int cfd);

// 发送文件
int sendFile(int cfd, const char *fileName);

// 发送响应头（状态行 + 响应头）
int sendHeadMsg(int cfd, int status, const char *descr, const char *type,
                int length);

// 获取文件类型
const char *getFileType(const char *name);

// 发送目录
int sendDir(int cfd, const char *dirName);

// 解码
// from 被解码的数据，传入参数；to 储存解码之后的数据，传出参数
void decodeMsg(char *to, char *from);

// 将字符转换为整形数
int hexToDec(char c);
#endif // !__HTTP_SERVER_VERSION1
