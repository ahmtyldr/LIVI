#!/usr/bin/env bash
# Prints the boot timeline of the current boot in seconds since kernel start.
set -u
J=$(sudo journalctl -b --no-pager -o short-monotonic | sed -E 's/^\[ *//; s/\] [^ ]+ / /')
L=~/.config/LIVI/log/LIVI.log
ts() { echo "$J" | grep -m1 -E "$1" | awk '{printf "%.1f", $1}'; }
echo "kernel+userspace : $(systemd-analyze time 2>/dev/null | head -1 | sed 's/Startup finished in //')"
echo "NetworkManager   : $(ts 'Started NetworkManager.service')"
echo "kiosk started    : $(ts 'Started livi-kiosk.service')"
echo "cage on seat     : $(ts 'seatd.*New client connected')"
echo "LIVI main log    : $(ts 'hostOutput\] HDMI')"
echo "GStreamer init   : $(ts 'GStreamer 1\.')"
echo "UI enter         : $(ts '\[kiosk\] enter')"
echo "AP up            : $(ts 'AP up')"
echo "phone connected  : $(ts 'wireless connection from')"
echo "first video      : $(ts 'VideoChannel\] stream started')"
echo "blame top5       :"; systemd-analyze blame 2>/dev/null | head -5 | sed 's/^/   /'
