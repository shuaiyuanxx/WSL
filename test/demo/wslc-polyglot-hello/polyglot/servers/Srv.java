import java.net.*; import java.io.*;
public class Srv { public static void main(String[] a) throws Exception {
  int p=Integer.parseInt(a[0]); try(ServerSocket ss=new ServerSocket(p)){ Socket c=ss.accept();
    c.getInputStream().read(new byte[64]); c.getOutputStream().write("hello world from [java]\n".getBytes()); c.getOutputStream().flush();
    c.getInputStream().read(new byte[64]); c.close(); } } }
