#!/usr/bin/env bash
# 10 s samples: Wi-Fi throughput, tx retry ratio, failed frames, link rate, last BT/call event.
IW=/usr/sbin/iw
L=$HOME/.config/LIVI/log/LIVI.log
N0=$(wc -l < "$L")
printf "%-5s %-8s %-8s %-7s %-5s %-6s %s\n" "sn" "rxMbps" "txMbps" "retry%" "fail" "link" "olay"
sta() { sudo $IW dev wlan0 station dump | awk '/tx packets/{p=$3} /tx retries/{r=$3} /tx failed/{f=$3} /tx bitrate/{b=$3} END{print p+0, r+0, f+0, b+0}'; }
dev() { awk '/wlan0/{print $2, $10}' /proc/net/dev; }
for i in $(seq 1 18); do
  read -r rx1 tx1 <<< "$(dev)"; read -r p1 r1 f1 _ <<< "$(sta)"
  sleep 10
  read -r rx2 tx2 <<< "$(dev)"; read -r p2 r2 f2 br <<< "$(sta)"
  ev=$(tail -n +$((N0+1)) "$L" | grep -oiE 'hfp\][^[]{0,28}|sco\][^[]{0,28}|\[phone\][^[]{0,28}|AudioFocus type=[0-9]+\([A-Z_]+\)|call[A-Za-z]*[^[]{0,18}|keyframe[^[]{0,15}|stall[^[]{0,15}|reconnect[^[]{0,15}' | tail -1 | cut -c1-40)
  N0=$(wc -l < "$L")
  dp=$((p2-p1)); dr=$((r2-r1)); df=$((f2-f1))
  rxm=$(awk -v a="$rx1" -v b="$rx2" 'BEGIN{printf "%.2f", (b-a)*8/10/1e6}')
  txm=$(awk -v a="$tx1" -v b="$tx2" 'BEGIN{printf "%.2f", (b-a)*8/10/1e6}')
  rp=$(awk -v p="$dp" -v r="$dr" 'BEGIN{printf "%.1f", p>0?100*r/p:0}')
  printf "%-5d %-8s %-8s %-7s %-5d %-6s %s\n" $((i*10)) "$rxm" "$txm" "$rp" "$df" "$br" "$ev"
done
echo "sicaklik: $(vcgencmd measure_temp)  CPU: $(top -bn2 -d3 | grep '^%Cpu' | tail -1 | awk '{print $2"%"}')"
