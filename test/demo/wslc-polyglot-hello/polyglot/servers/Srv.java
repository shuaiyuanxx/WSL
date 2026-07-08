import java.net.*;
import java.io.*;

public class Srv {
    public static void main(String[] a) throws Exception {
        int port = Integer.parseInt(a[0]);

        try (ServerSocket server = new ServerSocket(port)) {
            Socket conn = server.accept();

            conn.getInputStream().read(new byte[64]);  // Windows request
            conn.getOutputStream().write("hello world from [java]\n".getBytes());
            conn.getOutputStream().flush();
            conn.getInputStream().read(new byte[64]);  // Windows ack -> exit
            conn.close();
        }
    }
}
