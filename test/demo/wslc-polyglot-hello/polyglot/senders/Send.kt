import java.net.Socket
fun main(){ Socket("127.0.0.1",9098).use { it.getOutputStream().write("hello world from [kotlin]\n".toByteArray()) } }