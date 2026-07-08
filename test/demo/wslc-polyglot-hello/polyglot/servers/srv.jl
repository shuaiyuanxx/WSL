using Sockets

port = parse(Int, ARGS[1])

srv = listen(IPv4(0), port)
conn = accept(srv)
readavailable(conn)                            # Windows request
write(conn, "hello world from [julia]\n")
readavailable(conn)                            # Windows ack -> exit
close(conn)
close(srv)
