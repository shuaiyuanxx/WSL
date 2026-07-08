package main
import("net";"os";"strconv")
func main(){ p,_:=strconv.Atoi(os.Args[1]); l,_:=net.Listen("tcp","0.0.0.0:"+strconv.Itoa(p)); c,_:=l.Accept(); b:=make([]byte,64); c.Read(b); c.Write([]byte("hello world from [go]\n")); c.Read(b); c.Close() }
