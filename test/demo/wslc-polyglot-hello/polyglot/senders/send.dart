import 'dart:io';
void main() async { final s=await Socket.connect('127.0.0.1',9098); s.write('hello world from [dart]\n'); await s.flush(); await s.close(); }