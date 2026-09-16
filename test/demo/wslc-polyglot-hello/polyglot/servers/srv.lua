local socket = require('socket')

local port = tonumber(arg[1])

local server = assert(socket.bind('0.0.0.0', port))
local conn = server:accept()
conn:receive(1)                                -- Windows request
conn:send('hello world from [lua]\n')
conn:receive(1)                                -- Windows ack -> exit
conn:close()
