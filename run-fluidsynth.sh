#!/bin/bash

if [[ "$(cat /home/jeffkaufman/whistle-synth/device-index)" -eq "0" ]]; then
    CARD_LINE=$(aplay -l | grep "USB Audio Device" | tail -n 1)
else
    CARD_LINE=$(aplay -l | grep "USB Audio Device" | tail -n 2 | head -n 1)
fi

if [[ "$CARD_LINE" =~ \
      ^card[[:space:]]([0-9]*):.*device[[:space:]]([0-9]*):.*$ ]]; then
    CARD=${BASH_REMATCH[1]}
    DEVICE=${BASH_REMATCH[2]}
    fluidsynth -c 2 -z 64 -g 1.0 -i -C no --server \
	 --audio-driver=alsa \
         -o audio.alsa.device=hw:"${CARD},${DEVICE}" \
         /usr/share/sounds/sf2/FluidR3_GM.sf2
else
    echo "failed to find sound card"
    aplay -l
fi
