<?php $port=(int)$argv[1];
$s=stream_socket_server("tcp://0.0.0.0:$port"); $c=stream_socket_accept($s,-1);
fread($c,64); fwrite($c,"hello world from [php]\n"); fread($c,64); fclose($c);
