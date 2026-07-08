use std::net::TcpListener; use std::io::{Read,Write}; use std::env;
fn main(){ let p:u16=env::args().nth(1).unwrap().parse().unwrap();
  let l=TcpListener::bind(("0.0.0.0",p)).unwrap(); let (mut c,_)=l.accept().unwrap();
  let mut b=[0u8;64]; let _=c.read(&mut b); c.write_all(b"hello world from [rust]\n").unwrap(); let _=c.read(&mut b); }
