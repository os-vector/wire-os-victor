#!/usr/bin/env bash

if [ $# -eq 0 ]; then
	echo "Need an argument"
	exit 1
fi

GIT_PROJ_ROOT=`git rev-parse --show-toplevel`
VICOS_SDK_VERSION="$(cat "${GIT_PROJ_ROOT}/project/VICOS_SDK_VERSION")"
: ${VICOS_SDK:="${HOME}/.anki/vicos-sdk/dist/${VICOS_SDK_VERSION}"}

if [ ! -d "$VICOS_SDK" ]; then
	echo "Could not find vicos SDK"
	exit 1
fi

VICOS_BIN=$VICOS_SDK/prebuilt/bin
LLVM_VERSION=$VICOS_BIN/llvm-$1
if [ -e "$LLVM_VERSION" ]; then
	echo "$LLVM_VERSION"
	exit 0
fi

if [ -f "$VICOS_BIN/$1" ]; then
	echo "$VICOS_BIN/$1"
	exit 0
fi

echo "Could not find executable $1 in $VICOS_BIN"
exit 1
