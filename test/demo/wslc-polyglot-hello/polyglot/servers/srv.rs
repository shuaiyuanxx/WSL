use std::net::TcpListener;
use std::io::{Read, Write};
use std::env;

fn main() {
    let port: u16 = env::args().nth(1).unwrap().parse().unwrap();

    let listener = TcpListener::bind(("0.0.0.0", port)).unwrap();
    let (mut conn, _) = listener.accept().unwrap();

    let mut buf = [0u8; 64];
    let _ = conn.read(&mut buf);                        // Windows request
    conn.write_all(b"hello world from [rust]\n").unwrap();
    let _ = conn.read(&mut buf);                        // Windows ack -> exit
}
