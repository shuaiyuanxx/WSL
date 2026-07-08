use IO::Socket::INET; my $port=$ARGV[0];
my $s=IO::Socket::INET->new(LocalAddr=>'0.0.0.0',LocalPort=>$port,Proto=>'tcp',Listen=>1,ReuseAddr=>1);
my $c=$s->accept(); my $b; $c->recv($b,64); print $c "hello world from [perl]\n"; $c->recv($b,64); close($c);
