package main

import (
	"net"
	"os"
	"strconv"
)

func main() {
	port, _ := strconv.Atoi(os.Args[1])

	listener, _ := net.Listen("tcp", "0.0.0.0:"+strconv.Itoa(port))
	conn, _ := listener.Accept()

	buf := make([]byte, 64)
	conn.Read(buf) // Windows request
	conn.Write([]byte("hello world from [go]\n"))
	conn.Read(buf) // Windows ack -> exit
	conn.Close()
}
