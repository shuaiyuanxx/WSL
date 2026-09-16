declare const require: any;
declare const process: any;

const net = require('net');
const port = +process.argv[2];

const srv = net.createServer((conn: any) => {
  conn.once('data', () => {                     // Windows request
    conn.write('hello world from [typescript]\n');
    conn.once('data', () => {                   // Windows ack -> exit
      conn.end();
      srv.close();
    });
  });
});

srv.listen(port, '0.0.0.0');
