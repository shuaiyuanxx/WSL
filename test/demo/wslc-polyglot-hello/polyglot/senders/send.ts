declare const require: any;
const net = require('net');
const s = net.connect(9098, '127.0.0.1', () => { s.end('hello world from [typescript]\n'); });