port <- as.integer(commandArgs(TRUE)[1])

srv <- serverSocket(port)
con <- socketAccept(srv, blocking = TRUE, open = "r+b")
readLines(con, n = 1, warn = FALSE)            # Windows request
writeLines("hello world from [r]", con)
readLines(con, n = 1, warn = FALSE)            # Windows ack -> exit
close(con)
close(srv)
