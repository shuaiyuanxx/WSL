#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
int main(int argc,char**argv){ int p=atoi(argv[1]); int fd=socket(AF_INET,SOCK_STREAM,0);
  int one=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
  sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(p);
  bind(fd,(sockaddr*)&a,sizeof(a)); listen(fd,1); int c=accept(fd,0,0);
  char b[64]; read(c,b,64); const char*m="hello world from [c++]\n"; write(c,m,strlen(m)); read(c,b,64); close(c); return 0; }
