import Glibc
let port = UInt16(CommandLine.arguments[1])!
let fd = socket(AF_INET, Int32(SOCK_STREAM.rawValue), 0)
var one: Int32 = 1
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, socklen_t(MemoryLayout<Int32>.size))
var a = sockaddr_in()
a.sin_family = sa_family_t(AF_INET)
a.sin_addr.s_addr = INADDR_ANY
a.sin_port = port.bigEndian
withUnsafePointer(to: &a) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { _ = bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) } }
listen(fd, 1)
let c = accept(fd, nil, nil)
var b = [UInt8](repeating: 0, count: 64)
_ = read(c, &b, 64)
let m = "hello world from [swift]\n"
_ = m.withCString { send(c, $0, strlen($0), 0) }
_ = read(c, &b, 64)
close(c)
