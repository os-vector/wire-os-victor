#!/usr/bin/env bash
set -e
set -u

GIT=`which git`
if [ -z $GIT ]; then
  echo git not found
  exit 1
fi
: ${TOPLEVEL:=`$GIT rev-parse --show-toplevel`}

function vlog()
{
    echo "[fetch-build-deps] $*"
}

pushd "${TOPLEVEL}" > /dev/null 2>&1

vlog "Check brew installation."
is_brew=`which brew`
set -e

if [ -z "$is_brew" ]; then
   vlog "Brew not found. Please install it."
   exit 1
fi

vlog "Check homebrew dependencies"
./tools/build/tools/ankibuild/installBuildDeps.py \
    -d ninja \
    python3 \
    rsync \
    wget \
    curl

if [ -d "/Applications/Webots.app" ]; then
  vlog "check webots version"
  webotsVer=`cat /Applications/Webots.app/resources/version.txt`
  supportedVerFile=./simulator/supportedWebotsVersions.txt
  if ! grep -Fxq "$webotsVer" $supportedVerFile ; then
    vlog "Webots version $webotsVer is unsupported. Here are the supported versions:"
    cat $supportedVerFile
    exit 1
  fi
fi

vlog "Build output dirs"
mkdir -p generated
mkdir -p _build

if [[ ${DONT_ANIM} != "1" ]]; then
  vlog "Extract and encode animation and sound assets. This will take a few seconds."
	./project/buildScripts/dependencies.py -v --deps-file DEPS --externals-dir EXTERNALS
else
	vlog "Not extracting animation assets"
fi

#vlog "Configure audio library"
#./lib/audio/configure.py

popd > /dev/null 2>&1
