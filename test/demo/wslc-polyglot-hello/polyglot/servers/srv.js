const net = require('net');
const port = +process.argv[2];

const srv = net.createServer(conn => {
  conn.once('data', () => {                    // Windows request
    conn.write('hello world from [javascript]\n');
    conn.once('data', () => {                  // Windows ack -> exit
      conn.end();
      srv.close();
    });
  });
});

srv.listen(port, '0.0.0.0');
