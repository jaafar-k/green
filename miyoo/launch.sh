#!/bin/sh
sysdir=/mnt/SDCARD/.tmp_update
mediadir=/mnt/SDCARD/Media/PDF
greendir=$sysdir/bin/green
export HOME=/mnt/SDCARD/Saves/CurrentProfile/saves/green
mkdir -p "$HOME" "$HOME/fontconfig-cache"
# Fonts for Arabic TOC (Pango/fontconfig); DejaVuSans lives under greendir/fonts
export FONTCONFIG_FILE="$greendir/fonts.conf"
export FONTCONFIG_PATH="$greendir"

while :
do
	cd $sysdir
	./bin/st -q -e "$sysdir/script/file_selector.sh" "PDF Reader" "$mediadir" "pdf"
	retVal=$?
	echo "=================" "$retVal"
	if [ $retVal -eq 137 ] || [ ! -f "$mediadir/.selected.pls" ]; then
		exit
	fi
	selected_file=$(cat "$mediadir/.selected.pls")
	if [ ! -f "$selected_file" ]; then
		continue
	fi
	echo "$selected_file"
	LD_LIBRARY_PATH="$sysdir/lib/parasyte:$LD_LIBRARY_PATH" "$greendir/green" "$selected_file"
done
