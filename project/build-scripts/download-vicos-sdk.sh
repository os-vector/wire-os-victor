#!/usr/bin/env bash

TOOLCHAIN_VERSION_DEFAULT="5.2.1-r06"

cd ../../..
ORIGDIR="$(pwd)"

if [[ $1 != "" ]]; then
    TOOLCHAIN_VERSION="$1"
else
    TOOLCHAIN_VERSION=$TOOLCHAIN_VERSION_DEFAULT
fi

ADEPS="$HOME/.anki"

if [[ "$(uname -a)" == *"x86_64"* && "$(uname -a)" == *"Linux"* ]]; then
    HOST="amd64-linux"
    TINFO_HOST="amd64"
    elif [[ "$(uname -a)" == *"arm64"* && "$(uname -a)" == *"Darwin"* ]]; then
    HOST="arm64-macos"
    elif [[ "$(uname -a)" == *"aarch64"* && "$(uname -a)" == *"Linux"* ]]; then
    HOST="arm64-linux"
    TINFO_HOST="arm64"
else
    echo "This can only be run on x86_64 Linux, arm64 Linux, or arm64 macOS systems at the moment."
    echo "This will be fixed once I compile the new toolchain for more platforms."
    exit 1
fi

echo "vicos-sdk platform: $HOST"

if [[ ! -d "$ADEPS/vicos-sdk/dist/${TOOLCHAIN_VERSION}/prebuilt" ]]; then
    mkdir -p "$ADEPS/vicos-sdk/dist/${TOOLCHAIN_VERSION}"
    cd "$ADEPS/vicos-sdk/dist/${TOOLCHAIN_VERSION}"
	echo "Downloading vicos-sdk ${TOOLCHAIN_VERSION}..."
    wget -q --show-progress https://github.com/os-vector/wire-os-externals/releases/download/${TOOLCHAIN_VERSION}/vicos-sdk_${TOOLCHAIN_VERSION}_$HOST.tar.gz
	echo "Extracting vicos-sdk ${TOOLCHAIN_VERSION}..."
    tar -zxf vicos-sdk_${TOOLCHAIN_VERSION}_$HOST.tar.gz
    rm -f vicos-sdk_${TOOLCHAIN_VERSION}_$HOST.tar.gz
fi

#if [[ ! -f "$ADEPS/vicos-sdk/dist/${TOOLCHAIN_VERSION}/prebuilt/lib/libtinfo.so.5" ]] && [[ "$(uname -a)" == *"Linux"* ]]; then
#    echo "Patching compiler to include libtinfo5"
#    cp -r "$ORIGDIR/3rd/libtinfo5/$TINFO_HOST/libtinfo.so.5.9" "$ADEPS/vicos-sdk/dist/${TOOLCHAIN_VERSION}/prebuilt/lib/libtinfo.so.5"
#fi
