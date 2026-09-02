#!/bin/bash

SCRIPT_PATH=$(dirname $([ -L $0 ] && echo "$(dirname $0)/$(readlink -n $0)" || echo $0))
SCRIPT_NAME=`basename ${0}`
TOPLEVEL=$(cd "$SCRIPT_PATH/../.." && pwd)

cd "${TOPLEVEL}"

if [[ "${OSPATH}" == "" ]]; then
	echo "OSPATH wasn't provided"
	exit 1
fi

TO_RUN="$@"

if [[ $TO_RUN == "" ]]; then
	echo "usage: OSPATH=<path> run_chroot.sh /path/to/program/in/chroot --and --args"
	exit 1
fi

if [[ $COZMO == "1" ]]; then
	export IS_COZMO=1
fi

chroot "${OSPATH}" /bin/bash -c "LD_LIBRARY_PATH=/anki/lib VIC_ANIM_CONFIG=/anki/etc/config/platform_config.json VIC_ENGINE_CONFIG=/anki/etc/config/platform_config.json ${TO_RUN}"
