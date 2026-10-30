#!/bin/bash

export GIT_PROJ_ROOT="$(
  cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 \
  && git rev-parse --show-toplevel
)"

cd "${GIT_PROJ_ROOT}"
: ${VICOS_SDK:="${HOME}/.anki/vicos-sdk/dist/$(cat project/VICOS_SDK_VERSION)"}
echo '#DO NOT MODIFY THIS FILE. IF YOU WANT DIFFERENT DIAGNOSTICS, MODIFY tools/build/build-tools/clangd-template, then rebuild' > .clangd
sed "s|VICOS_SDK_REPLACE|${VICOS_SDK}|g" tools/build/build-scripts/clangd-template >> .clangd
