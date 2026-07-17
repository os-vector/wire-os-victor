#!/usr/bin/env bash

TOOLCHAIN_VERSION_DEFAULT="1.24.4"

if [[ $1 != "" ]]; then
    TOOLCHAIN_VERSION="$1"
else
    TOOLCHAIN_VERSION=$TOOLCHAIN_VERSION_DEFAULT
fi

ADEPS="$HOME/.anki"

if [[ "$(uname -a)" == *"x86_64"* && "$(uname -a)" == *"Linux"* ]]; then
    HOST="linux-amd64"
    elif [[ "$(uname -a)" == *"arm64"* && "$(uname -a)" == *"Darwin"* ]]; then
    HOST="darwin-arm64"
    elif [[ "$(uname -a)" == *"aarch64"* && "$(uname -a)" == *"Linux"* ]]; then
    HOST="linux-arm64"
else
    echo "This can only be run on x86_64 Linux, arm64 Linux, or arm64 macOS systems at the moment."
    echo "This will be fixed once I compile the new toolchain for more platforms."
    exit 1
fi

echo "Go platform: $HOST"

GO_NAME="go${TOOLCHAIN_VERSION}.${HOST}"
GO_VER="${TOOLCHAIN_VERSION}"

if [[ ! -d "$HOME/.anki/go/dist/$GO_VER/go" ]]; then
    mkdir -p "$HOME/.anki/go/dist/$GO_VER"
    cd "$HOME/.anki/go/dist/$GO_VER"
    echo "Downloading Go $GO_VER..."
    wget -q --show-progress "https://go.dev/dl/${GO_NAME}.tar.gz"
    echo "Extracting Go $GO_VER..."
    tar -zxf "${GO_NAME}.tar.gz"
    rm -f "${GO_NAME}.tar.gz"
fi

