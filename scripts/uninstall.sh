#!/bin/bash
# Removes zebra-crisp-mac and moves its printers back to the Apple Zebra ZPL and EPL2 drivers.
# Usage: sudo /Library/Printers/ZebraCrisp/uninstall.sh

export PATH=/usr/bin:/bin:/usr/sbin:/sbin

if [ "$(id -u)" -ne 0 ]; then
  echo "Run this script with sudo: sudo $0" >&2
  exit 1
fi

OPTIONS="PageSize Resolution zeMediaTracking MediaType Darkness zePrintRate zeLabelTop zePrintMode zeTearOffPosition zeErrorReprint"

for ppd in /etc/cups/ppd/*.ppd; do
  [ -f "$ppd" ] || continue
  if grep -q '^\*NickName: "Zebra ZPL Label Printer Crisp"' "$ppd"; then
    language=ZPL
    apple_ppd=drv:///sample.drv/zebra.ppd
  elif grep -q '^\*NickName: "Zebra EPL2 Label Printer Crisp"' "$ppd"; then
    language=EPL2
    apple_ppd=drv:///sample.drv/zebraep2.ppd
  else
    continue
  fi
  queue="$(basename "$ppd" .ppd)"

  current="$(lpoptions -p "$queue" | tr ' ' '\n')"
  defaults=()
  for opt in $OPTIONS; do
    value="$(printf '%s\n' "$current" | sed -n "s/^$opt=//p" | head -1)"
    [ -n "$value" ] && defaults+=(-o "$opt-default=$value")
  done

  echo "Moving printer $queue back to the Apple Zebra $language driver"
  lpadmin -p "$queue" -m "$apple_ppd"
  # lpadmin ignores defaults given together with -m.
  [ ${#defaults[@]} -gt 0 ] && lpadmin -p "$queue" "${defaults[@]}"
done

rm -f /Library/Printers/PPDs/Contents/Resources/ZebraCrisp.ppd \
      /Library/Printers/PPDs/Contents/Resources/ZebraCrispEPL2.ppd
rm -f "/Library/PDF Services/Preview Zebra Output"
rm -rf /Library/Printers/ZebraCrisp
find /private/var/folders -mindepth 4 -maxdepth 4 -type d -name ZebraCrispPreview -prune -exec rm -rf {} + 2>/dev/null
pkgutil --forget com.github.jcl5m1.zebra-crisp-mac >/dev/null 2>&1

echo "zebra-crisp-mac is removed."
