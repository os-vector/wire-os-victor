#!/bin/bash

trap "exit 0" INT

while true; do
  if [[ -p /tmp/vector_audio.fifo ]]; then
    runuser -u "$SUDO_USER" -- /bin/bash -c "aplay -f S16_LE -c1 -r 32000 --buffer-time=80000 --period-time=20000 /tmp/vector_audio.fifo"
    sleep 0.2
  fi
done
