#!/bin/bash

SCRIPT_PATH=$(dirname $([ -L $0 ] && echo "$(dirname $0)/$(readlink -n $0)" || echo $0))
SCRIPT_NAME=`basename ${0}`
TOPLEVEL=$(cd "$SCRIPT_PATH/../.." && pwd)

cd "${TOPLEVEL}"

runuser -u "$SUDO_USER" -- /bin/bash -c "./simulator/controllers/vicCozmoBridge/vicCozmoBridge"
