#!/usr/bin/env bash
set -e
#set -u

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

function check_dep()
{
    CHECK_CMD="$*"
    eval $CHECK_CMD && return 0

    echo "Dependency check failed: $CHECK_CMD"
    echo "Make sure you have all of the dependencies."
    echo ""
    exit 1
}

pushd "${TOPLEVEL}" > /dev/null 2>&1

# Check for required programs
echo -n "Python 3: "
check_dep which python3
echo -n "Ninja: "
check_dep which ninja

vlog "Build output dirs"
mkdir -p generated
mkdir -p _build

if [[ ${DONT_ANIM} != "1" ]]; then
  vlog "Extract and encode animation and sound assets. This will take a few seconds."
	./project/buildScripts/dependencies.py -v --deps-file DEPS --externals-dir EXTERNALS
else
	vlog "Not extracting animation assets"
fi

vlog "Configure audio library"
#./lib/audio/configure.py

popd > /dev/null 2>&1
