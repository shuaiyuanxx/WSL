import java.net.Socket; import java.io.OutputStream;
public class Send { public static void main(String[] a) throws Exception {
  try(Socket s=new Socket("127.0.0.1",9098)){ s.getOutputStream().write("hello world from [java]\n".getBytes()); } } }