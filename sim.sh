#!/bin/bash

SCRIPT_PATH=$(dirname $([ -L $0 ] && echo "$(dirname $0)/$(readlink -n $0)" || echo $0))
SCRIPT_NAME=`basename ${0}`
TOPLEVEL=$(cd "$SCRIPT_PATH" && pwd)
OS_LATEST=$(curl http://ota.pvic.xyz/vic/latestVersion 2> /dev/null)
NOBUILD=0
COZMO=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -nb)
            NOBUILD=1
			shift
            ;;
        -cozmo)
            COZMO=1
            shift
            ;;
        -h|--help)
            echo "-nb = don't build victor"
			echo "-cozmo = cozmo"
            exit 0
            ;;
        *)
            echo "unknown option: $1"
            exit 1
            ;;
    esac
done

set -e

if [[ $? != "0" || $OS_LATEST == *"404 page"* || $OS_LATEST == "" ]]; then
	echo "failed to get current WireOS version from server: $OS_LATEST"
	echo "either curl isn't installed or we can't access the server"
	if [[ -n $(ls $TOPLEVEL/.sim/dvcbs/*/apq8009-robot-sysfs.img) ]]; then
		# terrible
		OS_LATEST=$(ls "$TOPLEVEL/.sim/dvcbs" | \grep 3)
		echo "i see $OS_LATEST is already there, using that"
	else
		echo "if you're cozmoing, run this script with internet once so required files can be downloaded"
		exit 1
	fi
fi

if [[ ! "$(uname -a)" == *"Linux"* ]]; then
	echo "this only works on Linux"
	exit 1
fi

if [[ $COZMO == "1" ]]; then
	if [[ ! "$(uname -a)" == *"x86_64"* && ! "$(uname -a)" == *"aarch64"* ]]; then
		echo "this only works on x86_64 and aarch64 right now"
		exit 1
	fi
else
	if [[ ! "$(uname -a)" == *"x86_64"* ]]; then
		echo "this only works on x86_64 right now"
		exit 1
	fi
fi

echo "latest OS: $OS_LATEST"

cd "$TOPLEVEL"

mkdir -p .sim
cd .sim

if [[ ! -f mprocs ]]; then
	mkdir -p mprocs
	wget https://github.com/pvolok/dekit/releases/download/v0.9.6/mprocs-0.9.6-linux-$(uname -m)-musl.tar.gz
	tar -zxvf mprocs-0.9.6-linux-$(uname -m)-musl.tar.gz
	chmod +rwx mprocs
fi

if [[ ! -d webots && $COZMO == "0" ]]; then
	wget https://github.com/cyberbotics/webots/releases/download/R2021b/webots-R2021b-x86-64.tar.bz2
	echo "extracting webots..."
	tar -xf webots-R2021b-x86-64.tar.bz2
	rm webots-R2021b-x86-64.tar.bz2
fi

if [[ $COZMO == "0" ]]; then
	cd "${TOPLEVEL}/simulator/controllers/vicBodyBridge"
	WEBOTS_HOME="${TOPLEVEL}/.sim/webots" make
	cd "${TOPLEVEL}/simulator/plugins/robot_windows/vicPanel"
	WEBOTS_HOME="${TOPLEVEL}/.sim/webots" make
fi
if [[ $COZMO == "1" ]]; then
	cd "${TOPLEVEL}/simulator/controllers/vicCozmoBridge"
	rm -f vicCozmoBridge
	make
fi
cd "${TOPLEVEL}/.sim"

if [[ ! -d dvcbs ]]; then
	git clone https://github.com/kercre123/dvcbs -b mountonly --depth=1
fi

cd dvcbs
if [[ ! -d "$OS_LATEST" ]]; then
	mkdir "$OS_LATEST"
	cd "$OS_LATEST"
	wget "http://ota.pvic.xyz/vic/latest/dev.ota"
	cd ..
fi
OSPATH="$(pwd)/${OS_LATEST}/edits"
sudo ./dvcbs.sh -m "$OS_LATEST"
touch /tmp/isSimMounted

if [[ ! -f "$OSPATH/tmp/isSimMounted" ]]; then
	sudo rm "$OSPATH/etc/resolv.conf"
	sudo touch "$OSPATH/etc/resolv.conf"
	sudo mount --bind /dev "$OSPATH/dev"
	sudo mount --bind /run "$OSPATH/run"
	sudo mount --bind /tmp "$OSPATH/tmp"
	sudo mount --bind /sys "$OSPATH/sys"
	sudo mount --bind /proc "$OSPATH/proc"
	sudo mount --bind /etc/resolv.conf "$OSPATH/etc/resolv.conf"
fi

cd "${TOPLEVEL}"

if [[ $NOBUILD == "0" ]]; then
	./project/victor/build-victor.sh -c Simulator
	./project/victor/scripts/stage.sh -c Simulator
fi
echo "transferring built anki folder to image..."
sudo rsync -ar --delete _build/staging/Simulator/anki "${OSPATH}/"

if [[ ! -d /tmp/anki/gateway ]]; then
	mkdir -p /tmp/anki/gateway
	openssl req -config tools/sdk/scripts/mac_cert.conf -subj "/C=US/ST=California/L=SF/O=Anki/CN=Vector-Local" -new -x509 -days 1000 -newkey rsa:2048 -nodes -keyout /tmp/anki/gateway/trust.key -out /tmp/anki/gateway/trust.cert
fi
#cd "${TOPLEVEL}/simulator/mprocs"

MPROCSTOUSE="simulator/mprocs/mprocs.yaml"
if [[ $COZMO == "1" ]]; then
	MPROCSTOUSE="simulator/mprocs/cozmo.yaml"
fi

if [[ $COZMO == "1" ]]; then
	echo "now connect to your cozmo's wifi network...."
	./simulator/controllers/vicCozmoBridge/vicCozmoBridge --conn
	if [[ $? == "1" ]]; then
		echo "robot connection timed out"
		exit 1
	fi
fi

sleep 1

sudo OSPATH="$OSPATH" \
	DISPLAY="$DISPLAY" \
	WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
	XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}" \
	XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
	"${TOPLEVEL}/.sim/mprocs" -c ${MPROCSTOUSE}
#sudo OSPATH=$OSPATH ./simulator/mprocs/run_chroot.sh /anki/bin/vic-engine

sudo umount "$OSPATH/dev"
sudo umount "$OSPATH/run"
sudo umount "$OSPATH/tmp"
sudo umount "$OSPATH/sys"
sudo umount "$OSPATH/proc"
sudo umount "$OSPATH/etc/resolv.conf"

echo
echo "unmounted"
