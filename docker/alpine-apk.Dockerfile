FROM alpine:3.22

ARG UID=1000

RUN apk add --no-cache \
    alpine-sdk \
    sudo \
    cmake \
    ninja \
    g++ \
    pkgconf \
    libdrm-dev \
    qt6-qtbase-dev \
    qt6-qtdeclarative-dev \
    libx11-dev \
    libxrandr-dev \
    libxi-dev

# abuild must not run as root.
RUN adduser -D -u "$UID" builder \
    && addgroup builder abuild \
    && echo "builder ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers \
    && mkdir -p /var/cache/distfiles \
    && chmod a+w /var/cache/distfiles \
    && su builder -c "abuild-keygen -a -n" \
    && cp /home/builder/.abuild/*.pub /etc/apk/keys/

USER builder
ENV PACKAGER="TiltBack <tiltback@localhost>"
ENV SRCDEST=/src
WORKDIR /src
