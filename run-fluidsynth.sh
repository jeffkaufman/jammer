#!/bin/bash

CARD_LINE=$(aplay -l | grep "USB Audio Device" | tail -n 1)
if [[ "$CARD_LINE" =~ \
      ^card[[:space:]]([0-9]*):.*device[[:space:]]([0-9]*):.*$ ]]; then
    CARD=${BASH_REMATCH[1]}
    DEVICE=${BASH_REMATCH[2]}
    fluidsynth -g 2.0 -i -c=2 -z=8 --server --audio-driver=alsa \
         -o audio.alsa.device=hw:"${CARD},${DEVICE}" \
         /usr/share/sounds/sf2/FluidR3_GM.sf2
else
    echo "failed to find sound card"
    aplay -l
fi
