<?php
$port = (int)$argv[1];

$server = stream_socket_server("tcp://0.0.0.0:$port");
$conn = stream_socket_accept($server, -1);
fread($conn, 64);                              // Windows request
fwrite($conn, "hello world from [php]\n");
fread($conn, 64);                              // Windows ack -> exit
fclose($conn);
