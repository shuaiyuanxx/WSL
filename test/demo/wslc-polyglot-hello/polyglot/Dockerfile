FROM debian:stable-slim

WORKDIR /app

# apt-installable toolchains (validated on debian:stable-slim). Includes the
# apt-provided new languages: sbcl (Lisp), node-typescript (TypeScript, provides
# `tsc`), plus curl/unzip/gnupg used to fetch the non-apt toolchains below.
# ghc (Haskell) and zig are NOT installed here — they are build-only compilers
# installed+used+purged within a single RUN below so they never bloat the image.
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 default-jdk nodejs r-base rustc golang php-cli ruby perl \
        swi-prolog lua5.4 \
        gcc g++ gfortran gnat gnucobol ocaml-nox binutils libc6-dev \
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

# (Zig is fetched, used, and deleted inside the hello_zig compile RUN below so
# its ~340 MB never lands in a persistent layer.)

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
 && kotlinc /app/hello/Hello.kt -include-runtime -d /app/bin/hello_kt.jar \
 && tsc --outFile /app/bin/hello_ts.js /app/hello/hello.ts

# Haskell: install ghc, compile the static binary, then purge ghc — all in one
# RUN so the ~700 MB compiler never persists in a layer. The GHC-compiled binary
# still dynamically links libnuma.so.1 (a tiny runtime lib pulled in by ghc), so
# reinstall just libnuma1 after purging ghc or the binary won't load.
RUN apt-get update && apt-get install -y --no-install-recommends ghc \
 && ghc -o /app/bin/hello_haskell /app/hello/hello.hs -outputdir /tmp/ghc \
 && apt-get purge -y ghc && apt-get autoremove -y \
 && apt-get install -y --no-install-recommends libnuma1 \
 && rm -rf /var/lib/apt/lists/* /tmp/ghc

# Zig: fetch the toolchain, compile the static binary, then delete it — all in
# one RUN so the ~340 MB toolchain never persists in a layer.
RUN curl -fsSL https://ziglang.org/download/0.13.0/zig-linux-x86_64-0.13.0.tar.xz -o /tmp/zig.tar.xz \
 && mkdir -p /opt/zig && tar -xJf /tmp/zig.tar.xz -C /opt/zig --strip-components=1 \
 && /opt/zig/zig build-exe /app/hello/hello.zig -femit-bin=/app/bin/hello_zig -O ReleaseSafe \
 && rm -rf /opt/zig /tmp/zig.tar.xz

COPY servers/ /app/servers/

# Compile the compiled-language servers into /app/servers/bin (interpreted
# servers run from source at container start). Validated by the 18/18 probe.
RUN mkdir -p /app/servers/bin \
 && gcc  -o /app/servers/bin/srv_c   /app/servers/srv.c \
 && g++  -o /app/servers/bin/srv_cpp /app/servers/srv.cpp \
 && rustc -o /app/servers/bin/srv_rust /app/servers/srv.rs \
 && (cd /app/servers && go build -o /app/servers/bin/srv_go srv.go) \
 && ocamlopt unix.cmxa /app/servers/srv.ml -o /app/servers/bin/srv_ocaml \
 && kotlinc /app/servers/Srv.kt -include-runtime -d /app/servers/bin/srv_kt.jar \
 && (cd /app/servers && javac Srv.java -d /app/servers/bin) \
 && tsc --outDir /app/servers/bin /app/servers/srv.ts \
 && (cd /app/servers && dart compile exe srv.dart -o /app/servers/bin/srv_dart)

# The Windows app drives everything via CreateProcess against a running
# container, so pid-1 just needs to stay alive. The app overrides this with its
# own InitProcess (sleep infinity); this CMD is a sane fallback for `wslc run`.
CMD ["sleep", "infinity"]
