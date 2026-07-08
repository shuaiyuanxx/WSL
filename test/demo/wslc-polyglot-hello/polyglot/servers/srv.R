port <- as.integer(commandArgs(TRUE)[1])
srv <- serverSocket(port); con <- socketAccept(srv, blocking=TRUE, open="r+b")
readLines(con, n=1, warn=FALSE); writeLines("hello world from [r]", con); readLines(con, n=1, warn=FALSE); close(con); close(srv)
