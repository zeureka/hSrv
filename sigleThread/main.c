#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char *argv[]) {
  if (argc < 3) {
    printf("eg: ./a.out port path\n");
    return -1;
  }
  unsigned short port = atoi(argv[1]);
  // 切换服务器的工作路径
  chdir(argv[2]);
  // 初始化监听套接字
  int lfd = initListenFd(port);
  // 启动服务器程序
  epollRun(lfd);
  return EXIT_SUCCESS;
}
