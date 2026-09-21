FROM alpine:3.22

RUN apk add --no-cache \
    cmake \
    ninja \
    g++ \
    pkgconf \
    libdrm-dev \
    qt6-qtbase-dev \
    qt6-qtdeclarative-dev

WORKDIR /src
