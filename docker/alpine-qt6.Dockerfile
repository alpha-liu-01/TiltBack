FROM alpine:3.22

RUN apk add --no-cache \
    cmake \
    ninja \
    g++ \
    qt6-qtbase-dev \
    qt6-qtdeclarative-dev

WORKDIR /src
