import 'dart:io';
void main(List<String> a) async { final p=int.parse(a[0]); final ss=await ServerSocket.bind('0.0.0.0',p);
  await for (final c in ss){ c.listen((d){ c.write('hello world from [dart]\n'); }); await Future.delayed(Duration(milliseconds:800)); c.destroy(); break; } await ss.close(); }
