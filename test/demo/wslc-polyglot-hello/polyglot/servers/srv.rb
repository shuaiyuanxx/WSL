require 'socket'

port = ARGV[0].to_i

server = TCPServer.new('0.0.0.0', port)
conn = server.accept
conn.recv(64)                                  # Windows request
conn.write("hello world from [ruby]\n")
conn.recv(64)                                  # Windows ack -> exit
conn.close
