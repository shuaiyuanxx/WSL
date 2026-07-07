use std::net::TcpStream; use std::io::Write;
fn main(){ let mut s=TcpStream::connect("127.0.0.1:9098").unwrap(); s.write_all(b"hello world from [rust]\n").unwrap(); }