import socket,sys
p=int(sys.argv[1]); s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
s.bind(('0.0.0.0',p)); s.listen(1)
c,_=s.accept(); c.recv(64)                       # Windows request
c.sendall(b'hello world from [python]\n')
c.recv(64); c.close()                            # Windows ack -> exit
