import 'dart:io';

void main(List<String> a) async {
  final port = int.parse(a[0]);

  final server = await ServerSocket.bind('0.0.0.0', port);

  await for (final conn in server) {
    conn.listen((d) {                                  // Windows request
      conn.write('hello world from [dart]\n');
    });
    await Future.delayed(Duration(milliseconds: 800)); // Windows ack -> exit
    conn.destroy();
    break;
  }

  await server.close();
}
