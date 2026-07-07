package main
import ("net")
func main(){ c,_:=net.Dial("tcp","127.0.0.1:9098"); c.Write([]byte("hello world from [go]\n")); c.Close() }