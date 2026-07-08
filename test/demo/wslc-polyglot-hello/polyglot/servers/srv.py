import socket
import sys

port = int(sys.argv[1])

s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', port))
s.listen(1)

conn, _ = s.accept()
conn.recv(64)                                  # Windows request
conn.sendall(b'hello world from [python]\n')
conn.recv(64)                                  # Windows ack -> exit
conn.close()
