FROM debian:stable-slim

WORKDIR /app

# One layer for all ~20 toolchains (validated on debian:stable-slim).
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 default-jdk nodejs r-base rustc golang php-cli ruby perl \
        swi-prolog lua5.4 \
        gcc g++ gfortran gnat gnucobol ocaml-nox ghc binutils libc6-dev \
        ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY hello/ /app/hello/
COPY run-all.sh /app/run-all.sh
RUN chmod +x /app/run-all.sh

# Compile the compiled languages into /app/bin (runtime just executes these).
RUN mkdir -p /app/bin \
 && gcc      -O2 -o /app/bin/hello_c        /app/hello/hello.c \
 && g++      -O2 -o /app/bin/hello_cpp      /app/hello/hello.cpp \
 && rustc    -O  -o /app/bin/hello_rust     /app/hello/hello.rs \
 && (cd /app/hello && go build -o /app/bin/hello_go hello.go) \
 && gnatmake -o /app/bin/hello_ada /app/hello/hello.adb -D /tmp \
 && as -o /tmp/hello.o /app/hello/hello.s && ld -o /app/bin/hello_asm /tmp/hello.o \
 && gfortran -O2 -o /app/bin/hello_fortran  /app/hello/hello.f90 \
 && cobc -x -o /app/bin/hello_cobol /app/hello/hello.cob \
 && ocamlopt -o /app/bin/hello_ocaml /app/hello/hello.ml \
 && ghc -o /app/bin/hello_haskell /app/hello/hello.hs -outputdir /tmp/ghc

CMD ["/app/run-all.sh"]
