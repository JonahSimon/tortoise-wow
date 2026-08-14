# Tortoise-WoW 1.18.1 (build 7272) — build + runtime image, built from this repo.
#
# Debian trixie because INSTALL-LINUX.md pins GCC 14.2 / CMake 3.31 / ACE 8.0.2 /
# Boost 1.83, and trixie is where those line up. ACE must be 7.x or newer: the
# tree is C++17, which removed dynamic exception specifications, and ACE 6.x still
# uses them — its headers bury WorldSocketMgr.cpp in errors.
#
# Two stages: the builder keeps the ~4 GB of objects and the extractor binaries,
# the runtime carries only what mangosd/realmd need.

# ---------------------------------------------------------------- build stage
FROM debian:trixie AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake git \
      libace-dev libboost-all-dev \
      default-libmysqlclient-dev libssl-dev zlib1g-dev libbz2-dev \
 && rm -rf /var/lib/apt/lists/*

# Build context is the repo root, so the repo lands at /src directly.
COPY . /src

# 16 CPUs / 24 GB in the Docker VM (see docs/DOCKER.md and C:\Users\mihov\.wslconfig
# on Windows hosts running the WSL2 backend). -j10 is what fits: each heavy
# translation unit in this tree peaks around 1-2 GB, so 10 concurrent jobs can
# reach ~20 GB worst case, leaving headroom for the OS and Docker daemon inside
# the VM. CPU is not the binding constraint here — memory is. Lower via
# --build-arg BUILD_JOBS=N (or the scripts/rebuild.sh BUILD_JOBS env var) if the
# VM's memory allocation is ever reduced, or if the VM OOMs mid-compile.
ARG BUILD_JOBS=10

# CMAKE_INSTALL_PREFIX is COMPILED IN (SYSCONFDIR is baked at build time), so the
# prefix chosen here is where the binaries look for their configs forever. Moving
# an install after the fact produces "AI Playerbot is Disabled. Unable to open
# configuration file".
#
#   BUILD_PLAYERBOTS    defaults OFF -> no bots, and NO warning.
#   USE_EXTRACTORS      defaults OFF -> no mapextractor/vmapextractor/MoveMapGen.
#   ALLOW_TURTLE_ADDONS defaults ON and must STAY on, or the client crashes with
#                       "interface corrupt" the moment you enter the world.
RUN cmake -B /build -S /src \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/opt/turtle \
      -DBUILD_PLAYERBOTS=ON \
      -DUSE_EXTRACTORS=ON \
      -DALLOW_TURTLE_ADDONS=ON \
 && cmake --build /build -j"${BUILD_JOBS}" \
 && cmake --install /build

# The extractors install next to the server binaries in some trees and stay in
# the build dir in others; collect whichever exist at one predictable path.
RUN mkdir -p /opt/turtle/extractors \
 && for t in mapextractor vmapextractor vmap_assembler MoveMapGen movemapgen; do \
        f=$(find /build /opt/turtle -maxdepth 4 -type f -name "$t" -perm -u+x 2>/dev/null | head -1); \
        [ -n "$f" ] && cp "$f" /opt/turtle/extractors/ || true; \
    done \
 && ls -la /opt/turtle/extractors

# -------------------------------------------------------------- runtime stage
FROM debian:trixie-slim AS runtime

# Exactly what `ldd mangosd` reports in the build stage, not a guess:
#   libACE-8.0.2 libboost_filesystem libboost_thread libcrypto libssl
#   libmariadb libz libzstd (+ libc/libm/libstdc++/libgcc from the base)
# Shipping libboost-system instead of libboost-thread makes mangosd die instantly
# with "error while loading shared libraries: libboost_thread.so.1.83.0", while
# realmd — which links fewer libraries — starts fine and makes it look like a
# config problem.
RUN apt-get update && apt-get install -y --no-install-recommends \
      libace-8.0.2 libboost-filesystem1.83.0 libboost-thread1.83.0 \
      libmariadb3 libssl3 zlib1g libzstd1 libbz2-1.0 \
      ca-certificates tini netcat-openbsd \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/turtle /opt/turtle

# Provenance. These sit in the runtime stage on purpose: an ARG here cannot
# invalidate the ~30-minute compile layer above, so stamping costs nothing.
# .git is excluded from the build context, so the binary cannot learn its own
# commit — it has to arrive as a build arg.
ARG GIT_SHA=unknown
ARG GIT_DIRTY=unknown
ARG DOCKERFILE_SHA=unknown
LABEL org.opencontainers.image.revision="$GIT_SHA"
LABEL com.turtle.source-dirty="$GIT_DIRTY"
LABEL com.turtle.dockerfile-sha256="$DOCKERFILE_SHA"

# Data (dbc/maps/vmaps/mmaps) and the generated configs are bind-mounted in, so
# the image stays reusable across re-extractions and config edits.
WORKDIR /opt/turtle/bin
ENTRYPOINT ["/usr/bin/tini", "-g", "--"]
