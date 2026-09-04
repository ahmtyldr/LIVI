#!/usr/bin/env bash
# Step 2: stop NetworkManager's netplan import loop (~14 s at boot).
# Debian's NM runs `netplan generate` + `systemctl reload NetworkManager` once
# per "netplan-*" profile while starting. Persist the profiles as plain NM
# keyfiles and drop the netplan YAML so nothing is netplan-prefixed any more.
set -euo pipefail
B=~/pi3-backup/step2; mkdir -p "$B"
sudo cp -a /etc/netplan "$B/netplan"
sudo cp -a /run/NetworkManager/system-connections "$B/run-connections"
sudo chown -R "$USER" "$B"
for f in /run/NetworkManager/system-connections/netplan-*.nmconnection; do
  [ -e "$f" ] || continue
  base=$(basename "$f" .nmconnection); new=${base#netplan-}
  sudo sed -E 's/^id=netplan-/id=/' "$f" | sudo tee "/etc/NetworkManager/system-connections/$new.nmconnection" >/dev/null
  sudo chmod 600 "/etc/NetworkManager/system-connections/$new.nmconnection"
  echo "keyfile: $new.nmconnection"
done
sudo rm -f /etc/netplan/90-NM-*.yaml
sudo chmod 600 /lib/netplan/00-network-manager-all.yaml 2>/dev/null || true
# safety net: if eth0 has no IPv4 two minutes after boot, put netplan back
sudo tee /usr/local/lib/livi/pi3-net-safety.sh >/dev/null <<'EOS'
#!/usr/bin/env bash
sleep 120
if ! ip -4 -br addr show eth0 | grep -q "192\.\|10\.\|172\."; then
  cp /home/livilite/pi3-backup/step2/netplan/90-NM-*.yaml /etc/netplan/ 2>/dev/null
  rm -f /etc/NetworkManager/system-connections/eth0.nmconnection
  systemctl restart NetworkManager
  logger -t pi3-net-safety "eth0 had no IP, restored netplan config"
fi
EOS
sudo chmod +x /usr/local/lib/livi/pi3-net-safety.sh
sudo tee /etc/systemd/system/pi3-net-safety.service >/dev/null <<'EOS'
[Unit]
Description=Pi3 network safety net (temporary)
After=NetworkManager.service
[Service]
Type=oneshot
ExecStart=/usr/local/lib/livi/pi3-net-safety.sh
[Install]
WantedBy=multi-user.target
EOS
sudo systemctl daemon-reload && sudo systemctl enable pi3-net-safety.service >/dev/null 2>&1
echo "step2 applied; backup in $B"
