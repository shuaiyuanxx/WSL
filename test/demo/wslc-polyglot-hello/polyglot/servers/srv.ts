declare const require:any; declare const process:any;
const net=require('net'); const port=+process.argv[2];
const srv=net.createServer((c:any)=>{ c.once('data',()=>{ c.write('hello world from [typescript]\n'); c.once('data',()=>{c.end(); srv.close();}); }); });
srv.listen(port,'0.0.0.0');
