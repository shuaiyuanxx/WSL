local socket=require('socket'); local port=tonumber(arg[1])
local s=assert(socket.bind('0.0.0.0',port)); local c=s:accept(); c:receive(1)
c:send('hello world from [lua]\n'); c:receive(1); c:close()
