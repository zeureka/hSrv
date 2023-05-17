#include "server.h"
#include <arpa/inet.h>
#include <assert.h> // 断言
#include <ctype.h>  // isxdigit()
#include <dirent.h> // 遍历目录
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

struct FDInfo {
    int fd;
    int epfd;
    pthread_t tid;
};

/*********************** initListenFd() *********************/
int initListenFd(unsigned short port) {
  // 错误判断变量
  int misJudgment = 0;

  // 创建监听的套接字，
  int lfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (-1 == lfd) {
    perror("socket error!");
    return -1;
  }

  // 设置服务端地址结构
  struct sockaddr_in srv_addr;
  memset(&srv_addr, 0, sizeof(srv_addr));
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(port);
  srv_addr.sin_addr.s_addr = INADDR_ANY;

  // 设置端口复用
  int opt = 1;
  setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // 绑定端口，
  misJudgment = bind(lfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
  if (-1 == misJudgment) {
    perror("bind error!");
    return -1;
  }

  // 设置监听
  misJudgment = listen(lfd, 32);
  if (-1 == misJudgment) {
    perror("listen error!");
    return -1;
  }

  // 返回监听套接字
  return lfd;
}

/*********************** epollRun() *********************/
int epollRun(int lfd) {
  // 错误判断变量
  int misJudgment = 0;

  // 创建红黑树根节点
  int epfd = epoll_create(1);
  if (-1 == epfd) {
    perror("epoll_create error!");
    return -1;
  }

  // 定义 lfd 的事件
  struct epoll_event ev;
  ev.data.fd = lfd;
  ev.events = EPOLLIN;

  // 把监听文件描述符挂在红黑树上
  misJudgment = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
  if (-1 == misJudgment) {
    perror("epoll_ctl ADD lfd error!");
    return -1;
  }

  // 定义 epoll_wait 返回的发生了读写事件的文件描述符的事件数组
  struct epoll_event evs[1024];
  int size = sizeof(evs) / sizeof(struct epoll_event);
  // 循环检测
  while (1) {
    int num = epoll_wait(epfd, evs, size, -1); // 阻塞检测

    for (int i = 0; i < num; ++i) {
      int fd = evs[i].data.fd;
      struct FDInfo* info = (struct FDInfo*)malloc(sizeof(struct FDInfo));
      info->epfd = epfd;
      info->fd = fd;
      if (fd == lfd) {
        // 建立新连接
        // acceptClient(epfd, lfd);
        pthread_create(&info->tid, NULL, acceptClient, info);
      } else {
        // 接收对端的数据
        // recvHttpRequest(epfd, fd);
        pthread_create(&info->tid, NULL, recvHttpRequest, info);
      }
    }
  }

  return 0;
}

/*********************** acceptClient() *********************/
void* acceptClient(void* arg) {
  struct FDInfo* info = (struct FDInfo*)arg;
  // 和客户端建立连接
  int cfd = accept(info->fd, NULL, NULL);
  if (-1 == cfd) {
    perror("accept error!");
    exit(-1);
  }

  // 设置非阻塞
  int flag = fcntl(cfd, F_GETFL); // 获取文件属性
  flag |= O_NONBLOCK;             // 把非阻塞属性添加进去
  fcntl(cfd, F_SETFL, flag);      // 把新的属性添加进去

  struct epoll_event ev;
  ev.data.fd = cfd;
  ev.events = EPOLLIN | EPOLLET; // 边沿非阻塞模式

  // 将新建立连接的通信文件描述符挂在红黑树上
  int misJudgment = epoll_ctl(info->epfd, EPOLL_CTL_ADD, cfd, &ev);
  if (-1 == misJudgment) {
    perror("epoll_ctl ADD cfd error!");
    exit(-1);
  }

  printf("accept %ld\n", pthread_self());
  pthread_detach(info->tid);
  free(info);
  info = NULL;
  arg = NULL;
  return NULL;
}

/*********************** recvHttpRequest() *********************/
void* recvHttpRequest(void* arg) {
  struct FDInfo* info = (struct FDInfo*)arg;

  printf("开始接收数据了\n");

  char buf[4096] = {0};
  char tmp[1024] = {0}; // 临时 buffer
  int len = 0;          // 每一次读取的长度
  int total = 0;        // buf 存取的偏移量

  // 循环接收数据
  while (1) {
    len = recv(info->fd, tmp, sizeof(tmp), 0);
    if (len <= 0) {
      break;
    }

    if (total + len < sizeof(buf)) {
      memcpy(buf + total, tmp, len);
    }

    total += len;
  }

  // 判断数据是否被接收完毕
  if (-1 == len && EAGAIN == errno) {
    // 解析请求行
    char *pt = strstr(buf, "\r\n"); // pt指向'\r'的位置
    int reqLen = pt - buf;          // 得到请求行的长度
    buf[reqLen] = '\0';
    parseRequestLine(buf, info->fd);
  } else if (0 == len) {
    // 客户断开了连接
    int misJudgment = epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, NULL);
    if (-1 == misJudgment) {
      perror("epoll_ctl DEL cfd error!");
      exit(-1);
    }

    close(info->fd);
  } else {
    perror("recvHttpRequest recv error!");
    exit(-1);
  }

  printf("recvMsg %ld\n", pthread_self());
  pthread_detach(info->tid);
  free(info);
  info = NULL;
  arg = NULL;
  return 0;
}

/*********************** parseRequestLine() *********************/
int parseRequestLine(const char *line, int cfd) {
  char method[8], path[1024];
  // 解析请求行
  sscanf(line, "%[^ ] %[^ ]", method, path);

  printf("method: %s, path: %s\n", method, path);

  // 不区分大小写比较
  if (strcasecmp(method, "GET") != 0) {
    return -1;
  }

  // 将 unicode码解码成中文
  decodeMsg(path, path);

  // 处理客户端请求静态资源（目录或文件）
  char *file = NULL;
  if (strcmp(path, "/") == 0) {
    // 把绝对路径切换成相对路径
    file = "./";
  } else {
    // 去掉相对路径中的 "/" eg: /xxx/a.png -> xxx/a.png
    file = path + 1;
  }

  // 判断 file 代表的是目录还是文件, 获取文件属性
  struct stat st;
  int misJudgment = stat(file, &st);
  if (-1 == misJudgment) {
    // file 对应的文件不存在, 回复 404 页面
    sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
    sendFile(cfd, "404.html");
    return 0;
  }

  if (S_ISDIR(st.st_mode)) {
    // 把本地目录的内容发送给客户端
    sendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
    sendDir(cfd, file);
  } else {
    // 把文件的内容发送个客户端
    sendHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size);
    printf("getFileType(file) = %s\n", getFileType(file));
    printf("path=%s\n", path);
    sendFile(cfd, file);
  }

  return 0;
}

/*********************** sendFile *********************/
int sendFile(int cfd, const char *fileName) {
  // 打开文件
  int fd = open(fileName, O_RDONLY);
  assert(fd > 0); // 断言

#if 0
  while (1) {
    char buf[1024] = {0};
    memset(buf, 0, sizeof(buf));
    int len = read(fd, buf, sizeof(buf));
    if (len > 0) {
      int misJudgment = send(cfd, buf, len, 0);
      if (-1 == misJudgment) {
        // 可以通过 goto语句实现重新发送
        perror("sendFile send error!");
        continue;
      }
      usleep(10); // 防止发送的太快了，接收端处理不过来 !!!!!
    } else if (0 == len) {
      printf("客户端断开连接...\n");
      // 客户端关闭
      break;
    } else {
      perror("sendFile recv error!");
      break;
    }
  }
#else
  // 求文件的大小
  int size =
      lseek(fd, 0, SEEK_END); // 此时文件指针已经在尾部，如果继续读会没有数据

  lseek(fd, 0, SEEK_SET); // 将文件指针移动到开始位置

  off_t offset = 0; // 偏移量
  // 循环写数据，防止文件过大，导致只读出部分数据
  while (offset < size) {
    // offset 是系统在读完数据之后自己修改，传入传出参数；零拷贝函数sendfile()
    int ret = sendfile(cfd, fd, &offset, size - offset); // 返回值: 发送的字节数
    // printf("ret value: %d\n", ret);
    if (-1 == ret) {
      if (errno == EAGAIN) {
        // 没有数据，可以再次尝试；
        // 不是因为sendfile函数异常导致的错误，是因为cfd发送fd写入的数据太快，两者的速度不一致
        // 导致cfd再次发送数据的时候，缓冲区里没有数据可发送（cfd为非阻塞，所以会持续发送数据），
        // 而造成返回值为 -1，这对程序不造成影响，所以可以直接 continue 这次循环
        // printf("没数据可发送...\n");
        continue;
      }
      perror("sendfile error!");
    }
  }
#endif

  close(fd);
  return 0;
}

/*********************** sendHeadMsg() *********************/
int sendHeadMsg(int cfd, int status, const char *descr, const char *type,
                int length) {
  // 状态行
  char buf[4096] = {0};
  sprintf(buf, "HTTP/1.1 %d %s\r\n", status, descr);
  // 响应头
  sprintf(buf + strlen(buf), "Content-Type: %s\r\n", type);
  sprintf(buf + strlen(buf), "Content-Length: %d\r\n", length);
  sprintf(buf + strlen(buf), "\r\n"); // 空行

  int misJudgment = send(cfd, buf, strlen(buf), 0);
  if (-1 == misJudgment) {
    perror("sendHeadMsg send error!");
  }

  return 0;
}

/*********************** sendDir() *********************/
int sendDir(int cfd, const char *dirName) {
  char buf[4096] = {0};
  sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirName);
  struct dirent **nameList; //  nameList是个指针数组(eg: struct dirent* tmp[]);
  int num = scandir(dirName, &nameList, NULL, alphasort);

  for (int i = 0; i < num; ++i) {
    // 取出对应文件的名字
    char *name = nameList[i]->d_name;
    struct stat st;
    char subPath[1024] = {0};
    sprintf(subPath, "%s/%s", dirName, name);
    int misJudgment = stat(subPath, &st);

    if (S_ISDIR(st.st_mode)) {
      sprintf(buf + strlen(buf),
              "<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>", name,
              name, st.st_size);
    } else {
      sprintf(buf + strlen(buf),
              "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>", name, name,
              st.st_size);
    }

    misJudgment = send(cfd, buf, strlen(buf), 0);
    if (-1 == misJudgment) {
      perror("sendDir send error!");
    }

    memset(buf, 0, sizeof(buf));
    free(nameList[i]);
  }

  sprintf(buf, "</table></body></html>");
  send(cfd, buf, strlen(buf), 0);
  free(nameList);
  return 0;
}

/*********************** getFileType() *********************/
const char *getFileType(const char *name) {
  // 自右向左查询 '.' 字符，如果不存在就返回 NULL
  const char *dot = strrchr(name, '.');

  if (NULL == dot) {
    return "text/pain; charset=utf-8";
  }

  if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) {
    return "text/html; charset=utf-8";
  }
  if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) {
    return "image/jpeg";
  }
  if (strcmp(dot, ".gif") == 0) {
    return "image/gif";
  }
  if (strcmp(dot, ".png") == 0) {
    return "image/png";
  }
  if (strcmp(dot, ".css") == 0) {
    return "text/css";
  }
  if (strcmp(dot, ".au") == 0) {
    return "audio/basic";
  }
  if (strcmp(dot, ".wav") == 0) {
    return "audio/wav";
  }
  if (strcmp(dot, ".avi") == 0) {
    return "video/x-msvideo";
  }
  if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0) {
    return "video/quicktime";
  }
  if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0) {
    return "video/mpeg";
  }
  if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0) {
    return "model/vrml";
  }
  if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0) {
    return "audio/midi";
  }
  if (strcmp(dot, ".mp3") == 0) {
    return "audio/mp3";
  }
  if (strcmp(dot, ".ogg") == 0) {
    return "application/ogg";
  }
  if (strcmp(dot, ".pac") == 0) {
    return "application/x-ns-proxy-autoconfig";
  }
  if (strcmp(dot, ".pdf") == 0) {
    return "application/pdf";
  }

  return "text/plain; charset=utf-8";
}

/*********************** decodeMsg() *********************/
void decodeMsg(char *to, char *from) {
  while (*from != '\0') {
    // isxdigit() 判断字符是不是十六进制格式
    if ('%' == from[0] && isxdigit(from[1]) && isxdigit(from[2])) {
      // 将十六进制的数转换为十进制，将这个数值赋值给字符
      *to = hexToDec(from[1]) * 16 + hexToDec(from[2]);
      // 跳过 from[1] 和 from[2]，因为在前面已经处理过了
      from += 2;
    } else {
      // 字符拷贝，赋值
      *to = *from;
    }
    ++to;
    ++from;
  }
  *to = '\0'; // 将传出参数的最后一个字符改为'\0'，
              // 用以截断返回的字符串，防止返回的字符串在 '.type' 结尾后
              // 紧跟着还有字符，导致最后跟踪文件时出错
}

/*********************** hexToDec() *********************/
int hexToDec(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return 0;
}
