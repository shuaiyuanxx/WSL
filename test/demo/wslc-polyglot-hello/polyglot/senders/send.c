#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
int main(){ int fd=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_port=htons(9098); inet_pton(AF_INET,"127.0.0.1",&a.sin_addr); connect(fd,(struct sockaddr*)&a,sizeof(a)); const char*m="hello world from [c]\n"; write(fd,m,strlen(m)); close(fd); return 0; }