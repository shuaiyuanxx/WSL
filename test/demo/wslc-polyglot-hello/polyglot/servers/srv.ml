let () =
  let port = int_of_string Sys.argv.(1) in
  let s = Unix.socket Unix.PF_INET Unix.SOCK_STREAM 0 in
  Unix.setsockopt s Unix.SO_REUSEADDR true;
  Unix.bind s (Unix.ADDR_INET (Unix.inet_addr_any, port));
  Unix.listen s 1;
  let (c,_) = Unix.accept s in
  let b = Bytes.create 64 in
  ignore (Unix.read c b 0 64);
  let m = "hello world from [ocaml]\n" in
  ignore (Unix.write_substring c m 0 (String.length m));
  ignore (Unix.read c b 0 64);
  Unix.close c
