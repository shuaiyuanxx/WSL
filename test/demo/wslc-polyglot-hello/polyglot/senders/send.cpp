#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
int main(){ int fd=socket(AF_INET,SOCK_STREAM,0); sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(9098); inet_pton(AF_INET,"127.0.0.1",&a.sin_addr); connect(fd,(sockaddr*)&a,sizeof(a)); const char*m="hello world from [c++]\n"; write(fd,m,strlen(m)); close(fd); return 0; }