using Sockets; port=parse(Int,ARGS[1]); srv=listen(IPv4(0),port); c=accept(srv)
readavailable(c); write(c,"hello world from [julia]\n"); readavailable(c); close(c); close(srv)
