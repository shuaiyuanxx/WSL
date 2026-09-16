use IO::Socket::INET;

my $port = $ARGV[0];

my $server = IO::Socket::INET->new(
    LocalAddr => '0.0.0.0',
    LocalPort => $port,
    Proto     => 'tcp',
    Listen    => 1,
    ReuseAddr => 1,
);

my $conn = $server->accept();
my $buf;
$conn->recv($buf, 64);                         # Windows request
print $conn "hello world from [perl]\n";
$conn->recv($buf, 64);                         # Windows ack -> exit
close($conn);
