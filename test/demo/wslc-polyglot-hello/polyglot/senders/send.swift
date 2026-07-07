import Glibc
let fd = socket(AF_INET, Int32(SOCK_STREAM.rawValue), 0)
var addr = sockaddr_in()
addr.sin_family = sa_family_t(AF_INET)
addr.sin_port = in_port_t(UInt16(9098).bigEndian)
_ = "127.0.0.1".withCString { inet_pton(AF_INET, $0, &addr.sin_addr) }
let rc = withUnsafePointer(to: &addr) {
    $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
        connect(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.stride))
    }
}
if rc == 0 {
    let msg = "hello world from [swift]\n"
    _ = msg.withCString { send(fd, $0, strlen($0), 0) }
}
close(fd)