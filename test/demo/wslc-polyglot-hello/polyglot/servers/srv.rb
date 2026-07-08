require 'socket'; port=ARGV[0].to_i
s=TCPServer.new('0.0.0.0',port); c=s.accept; c.recv(64)
c.write("hello world from [ruby]\n"); c.recv(64); c.close
