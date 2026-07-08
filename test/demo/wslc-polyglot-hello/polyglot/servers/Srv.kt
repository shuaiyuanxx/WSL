import java.net.ServerSocket

fun main(a: Array<String>) {
    val port = a[0].toInt()

    ServerSocket(port).use { server ->
        val conn = server.accept()

        conn.getInputStream().read(ByteArray(64))       // Windows request
        conn.getOutputStream().write("hello world from [kotlin]\n".toByteArray())
        conn.getOutputStream().flush()
        conn.getInputStream().read(ByteArray(64))       // Windows ack -> exit
        conn.close()
    }
}
