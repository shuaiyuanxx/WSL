FROM debian:stable-slim

WORKDIR /app

# apt-installable toolchains (validated on debian:stable-slim). Includes the
# apt-provided new languages: sbcl (Lisp), node-typescript (TypeScript, provides
# `tsc`), plus curl/unzip/gnupg used to fetch the non-apt toolchains below.
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 default-jdk nodejs r-base rustc golang php-cli ruby perl \
        swi-prolog lua5.4 \
        gcc g++ gfortran gnat gnucobol ocaml-nox ghc binutils libc6-dev \
        sbcl node-typescript lua-socket \
        curl unzip xz-utils gnupg apt-transport-https ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# ---- Non-apt toolchains (each in its own layer; validated by probe builds) ----

# Dart via Google's apt repo.
RUN curl -fsSL https://dl-ssl.google.com/linux/linux_signing_key.pub \
        | gpg --dearmor -o /usr/share/keyrings/dart.gpg \
 && echo "deb [signed-by=/usr/share/keyrings/dart.gpg] https://storage.googleapis.com/download.dartlang.org/linux/debian stable main" \
        > /etc/apt/sources.list.d/dart.list \
 && apt-get update && apt-get install -y --no-install-recommends dart \
 && rm -rf /var/lib/apt/lists/*
ENV PATH="/usr/lib/dart/bin:${PATH}"

# Zig via official tarball (not in Debian apt).
RUN curl -fsSL https://ziglang.org/download/0.13.0/zig-linux-x86_64-0.13.0.tar.xz -o /tmp/zig.tar.xz \
 && mkdir -p /opt/zig && tar -xJf /tmp/zig.tar.xz -C /opt/zig --strip-components=1 \
 && ln -s /opt/zig/zig /usr/local/bin/zig && rm /tmp/zig.tar.xz

# Julia via official tarball (1.11+ — 1.10 fails with an execstack error in this runtime).
RUN curl -fsSL https://julialang-s3.julialang.org/bin/linux/x64/1.11/julia-1.11.5-linux-x86_64.tar.gz -o /tmp/julia.tar.gz \
 && mkdir -p /opt/julia && tar -xzf /tmp/julia.tar.gz -C /opt/julia --strip-components=1 \
 && ln -s /opt/julia/bin/julia /usr/local/bin/julia && rm /tmp/julia.tar.gz

# Kotlin compiler via JetBrains GitHub release.
RUN curl -fsSL https://github.com/JetBrains/kotlin/releases/download/v2.0.21/kotlin-compiler-2.0.21.zip -o /tmp/kotlin.zip \
 && unzip -q /tmp/kotlin.zip -d /opt \
 && ln -s /opt/kotlinc/bin/kotlinc /usr/local/bin/kotlinc && rm /tmp/kotlin.zip

# Swift via the official swift.org Debian 12 toolchain + its Linux runtime deps.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libcurl4 libedit2 libgcc-s1 libpython3-dev libstdc++6 libxml2 libz3-dev \
        pkg-config tzdata zlib1g-dev libncurses6 \
 && rm -rf /var/lib/apt/lists/* \
 && curl -fsSL https://download.swift.org/swift-5.10.1-release/debian12/swift-5.10.1-RELEASE/swift-5.10.1-RELEASE-debian12.tar.gz -o /tmp/swift.tar.gz \
 && mkdir -p /opt/swift && tar -xzf /tmp/swift.tar.gz -C /opt/swift --strip-components=2 \
 && ln -s /opt/swift/bin/swift /usr/local/bin/swift && rm /tmp/swift.tar.gz

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
 && ghc -o /app/bin/hello_haskell /app/hello/hello.hs -outputdir /tmp/ghc \
 && zig build-exe /app/hello/hello.zig -femit-bin=/app/bin/hello_zig -O ReleaseSafe \
 && kotlinc /app/hello/Hello.kt -include-runtime -d /app/bin/hello_kt.jar \
 && tsc --outFile /app/bin/hello_ts.js /app/hello/hello.ts

COPY tcp_server.py /app/tcp_server.py
COPY senders/ /app/senders/

# Compile the compiled-language senders into /app/senders/bin (interpreted
# senders run from source at container start).
RUN mkdir -p /app/senders/bin \
 && gcc  -o /app/senders/bin/send_c   /app/senders/send.c \
 && g++  -o /app/senders/bin/send_cpp /app/senders/send.cpp \
 && rustc -o /app/senders/bin/send_rust /app/senders/send.rs \
 && (cd /app/senders && go build -o /app/senders/bin/send_go send.go) \
 && ocamlopt unix.cmxa /app/senders/send.ml -o /app/senders/bin/send_ocaml \
 && kotlinc /app/senders/Send.kt -include-runtime -d /app/senders/bin/send_kt.jar \
 && (cd /app/senders && javac Send.java -d /app/senders/bin) \
 && tsc /app/senders/send.ts \
 && (cd /app/senders && dart compile exe send.dart -o /app/senders/bin/send_dart)

CMD ["/app/run-all.sh"]
