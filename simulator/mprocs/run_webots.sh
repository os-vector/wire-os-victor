#!/bin/bash

# exit 0

SCRIPT_PATH=$(dirname $([ -L $0 ] && echo "$(dirname $0)/$(readlink -n $0)" || echo $0))
SCRIPT_NAME=`basename ${0}`
TOPLEVEL=$(cd "$SCRIPT_PATH/../.." && pwd)

cd "${TOPLEVEL}"

#sudo -u "$SUDO_USER" QTWEBENGINE_DISABLE_SANDBOX=1 ./.sim/webots/webots simulator/worlds/victorLinux.wbt
runuser -u "$SUDO_USER" -- /bin/bash -c "QTWEBENGINE_DISABLE_SANDBOX=1 ./.sim/webots/webots simulator/worlds/victorLinux.wbt"
