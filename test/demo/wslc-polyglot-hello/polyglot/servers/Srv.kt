import java.net.ServerSocket
fun main(a:Array<String>){ val p=a[0].toInt(); ServerSocket(p).use{ ss-> val c=ss.accept()
  c.getInputStream().read(ByteArray(64)); c.getOutputStream().write("hello world from [kotlin]\n".toByteArray()); c.getOutputStream().flush()
  c.getInputStream().read(ByteArray(64)); c.close() } }
