import socket
socket.create_connection(('127.0.0.1',9098)).sendall(b'hello world from [python]\n')