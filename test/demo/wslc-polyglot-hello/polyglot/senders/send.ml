let () =
  let s = Unix.socket Unix.PF_INET Unix.SOCK_STREAM 0 in
  Unix.connect s (Unix.ADDR_INET (Unix.inet_addr_of_string "127.0.0.1", 9098));
  let m = "hello world from [ocaml]\n" in
  ignore (Unix.write_substring s m 0 (String.length m));
  Unix.close s