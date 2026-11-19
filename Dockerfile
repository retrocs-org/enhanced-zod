# syntax=docker/dockerfile:1
###############################################################################
# Zod dedicated-server deployment image.
#
# Bundles the match orchestrator + the headless `zod -d` engine + game data in
# one image. The orchestrator is the entrypoint; it spawns one `zod -d` per
# match (process-per-match) on its match port pool and hands clients a host:port.
#
# `zod -d` is genuinely headless: it links SDL3 but never opens a window or audio
# device (SDL_Init(VIDEO|AUDIO) lives only in the client), so no X server / xvfb
# is needed in the image.
#
#   docker build -t zod-server .
#   docker run --rm -p 8080:8080 -p 2300-2305:2300-2305 \
#     -e ADVERTISE_HOST=<address-clients-can-reach> zod-server
#
# Deploying to an x86_64 VPS from an arm64 machine? build with:
#   docker buildx build --platform linux/amd64 -t zod-server .
###############################################################################

# --- Stage 1: build the Go orchestrator (static, stdlib only) ----------------
FROM golang:1 AS gobuild
WORKDIR /src
COPY orchestrator/ ./
# stdlib only - no module downloads needed. Static binary so it runs on any base.
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/orchestrator .

# Independent Zod dedicated-server agent (the web service uses PHP below).
FROM golang:1.26 AS webbuild
WORKDIR /src
COPY web-service/ ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/zod-agent ./cmd/zod-agent

# --- Stage 2: build the C++ engine (zod) against SDL3 ------------------------
FROM ubuntu:26.04 AS cppbuild
ARG JOBS=4
ARG ZOD_VERSION=docker
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config curl ca-certificates python3 \
      libsdl3-dev libsdl3-image-dev libsdl3-ttf-dev \
 && rm -rf /var/lib/apt/lists/*
# 26.04's libsdl3-mixer-dev (the new MIX_ API) hasn't reached the release pocket
# yet, so build SDL3_mixer 3.2.4 from source against the apt-provided SDL3 (same
# as CI). Decoders off - the engine only needs the MIX_ symbols to link.
# A second install into a DESTDIR stages the .so for an arch-agnostic runtime copy.
RUN curl -fsSL -o /tmp/mixer.tar.gz \
      https://github.com/libsdl-org/SDL_mixer/releases/download/release-3.2.4/SDL3_mixer-3.2.4.tar.gz \
 && echo "182a07c745375e113dc740d43964ff21b0be29f29f59876c4dbc4db3d32f6901  /tmp/mixer.tar.gz" | sha256sum -c - \
 && mkdir -p /tmp/mixer && tar -xzf /tmp/mixer.tar.gz -C /tmp/mixer --strip-components=1 \
 && cmake -S /tmp/mixer -B /tmp/mixer/build \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DBUILD_SHARED_LIBS=ON -DSDLMIXER_VENDORED=OFF -DSDLMIXER_SAMPLES=OFF \
      -DSDLMIXER_FLAC=OFF -DSDLMIXER_GME=OFF -DSDLMIXER_MOD=OFF \
      -DSDLMIXER_MP3=OFF -DSDLMIXER_MIDI=OFF -DSDLMIXER_OPUS=OFF \
      -DSDLMIXER_VORBIS=OFF -DSDLMIXER_WAVPACK=OFF \
 && cmake --build /tmp/mixer/build --parallel ${JOBS} \
 && cmake --install /tmp/mixer/build \
 && ldconfig
RUN mkdir -p /rootfs/data && chmod 700 /rootfs/data && chown 65532:65532 /rootfs/data
WORKDIR /src
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY tests/ ./tests/
# build only the engine target (skip tests / mapgen)
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DZOD_VERSION="${ZOD_VERSION}" -DZOD_MAP_EDITOR=OFF -DZOD_FETCH_SOUNDFONT=OFF \
 && cmake --build build --target zod --parallel ${JOBS}
# Stage exactly what the runtime needs into /rootfs, so the final image can be
# FROM scratch (no base OS at all). The headless server never inits video/audio,
# so the X11/Wayland/Mesa/audio backends SDL would dlopen are never needed - we
# ship only the binary's ldd closure (~3 MB), our own glibc + loader included.
#   - non-loader libs -> /rootfs/applibs (flat, arch-agnostic, found via LD_LIBRARY_PATH)
#   - the ELF interpreter -> its exact PT_INTERP path (e.g. /lib64/ld-linux-*),
#     which the kernel hard-codes, so it must stay where ldd reports it
#   - a writable /tmp (1777) for the orchestrator's status sockets + match logs
RUN set -eu; mkdir -p /rootfs/applibs /rootfs/tmp; chmod 1777 /rootfs/tmp; \
    ldd /src/build/zod-ee | awk '/=> \//{print $3}' | sort -u | while IFS= read -r lib; do \
      [ -f "$lib" ] && cp -L "$lib" /rootfs/applibs/; \
    done; \
    interp=$(ldd /src/build/zod-ee | awk '/ld-linux|ld\.so/{print $1; exit}'); \
    mkdir -p "/rootfs$(dirname "$interp")"; cp -L "$interp" "/rootfs$interp"

# --- Stage 3: runtime --------------------------------------------------------
# scratch: no base OS. Everything the two binaries need is copied in explicitly.
FROM scratch AS runtime
COPY --from=cppbuild /rootfs/ /
WORKDIR /app
COPY --from=gobuild  /out/orchestrator  /app/orchestrator
COPY --from=cppbuild /src/build/zod-ee      /app/build/zod-ee
# Game data the dedicated server actually reads (verified via strace): all of
# maps/ plus the planet tileinfo. The ~90 MB of client graphics under assets/
# are never opened by `zod -d`, so they're left out.
COPY maps/            /app/maps/
COPY assets/planets/  /app/assets/planets/
COPY map_list.txt default_settings.txt /app/

# Orchestrator config - override at run time. ADVERTISE_HOST MUST be an address
# clients can actually reach; 127.0.0.1 only works for same-host testing.
# LD_LIBRARY_PATH points the ELF loader at our flat lib dir (no ld.so.cache here).
ENV LD_LIBRARY_PATH=/applibs \
    ZOD_DIR=/app \
    ZOD_BIN=/app/build/zod-ee \
    ORCH_ADDR=:8080 \
    ADVERTISE_HOST=127.0.0.1 \
    EMPTY_EXIT_SECS=300 \
    MATCH_PORT_MIN=2300 \
    MATCH_PORT_MAX=2305

# Default surface: the API + a 6-match pool (2300-2305). EXPOSE is only metadata,
# but here it matches the baked default range so it's honest (not the old noisy
# 2300-2399). Publish them with -p (e.g. `-p 8080:8080 -p 2300-2305:2300-2305`);
# for more concurrent matches bump the published range AND MATCH_PORT_MIN/MAX.
EXPOSE 8080
EXPOSE 2300-2305/tcp
ENTRYPOINT ["/app/orchestrator"]

# Standalone Linux server: private by default, public only with explicit settings.
FROM runtime AS dedicated-runtime
COPY --from=webbuild /out/zod-agent /app/zod-agent
COPY --from=cppbuild /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
COPY --from=cppbuild --chown=65532:65532 /rootfs/data/ /data/
USER 65532:65532
ENV HOME=/data
VOLUME /data
EXPOSE 2137/tcp
ENTRYPOINT ["/app/zod-agent"]
CMD ["--mode", "private", "--state", "/data"]

# Central PHP website/API; MariaDB is deployed separately by compose.web.yaml.
FROM php:8.4-apache AS web-runtime
RUN apt-get update && apt-get install -y --no-install-recommends libonig-dev \
 && docker-php-ext-install pdo_mysql mbstring \
 && rm -rf /var/lib/apt/lists/* \
 && php -r 'if (!defined("PASSWORD_ARGON2ID")) { fwrite(STDERR,"Argon2id support required\n"); exit(1); }'
COPY web-service/php/ /var/www/zod/
COPY packaging/linux/apache.conf /etc/apache2/sites-available/000-default.conf
RUN printf 'expose_php=Off\ndisplay_errors=Off\nlog_errors=On\nmemory_limit=128M\npost_max_size=64K\nupload_max_filesize=64K\nmax_execution_time=15\n' > /usr/local/etc/php/conf.d/zod.ini
ENV ZOD_DB_HOST=db ZOD_DB_NAME=zod ZOD_DB_USER=zod
EXPOSE 80

# Real MariaDB + PHP API + headless engine contract tests.
FROM cppbuild AS cloud-test
ARG JOBS=4
RUN apt-get update && apt-get install -y --no-install-recommends php-cli php-mysql php-mbstring mariadb-server \
 && rm -rf /var/lib/apt/lists/*
COPY --from=webbuild /out/zod-agent /out/zod-agent
COPY web-service/ /src/web-service/
COPY maps/ /src/maps/
COPY assets/planets/ /src/assets/planets/
COPY map_list.txt default_settings.txt /src/
RUN cmake --build build --target zod_engine_regressions --parallel ${JOBS} \
 && web-service/tests/run-local.sh /out/zod-agent /src/build/zod-ee /src/build/zod_engine_regressions

# Preserve the existing orchestrator image as the default target.
FROM runtime AS default
