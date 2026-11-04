#!/bin/sh
sysdir=/mnt/SDCARD/.tmp_update
mediadir=/mnt/SDCARD/Media/PDF
greendir=$sysdir/bin/green
export HOME=/mnt/SDCARD/Saves/CurrentProfile/saves/green
mkdir -p "$HOME" "$HOME/fontconfig-cache"

while :
do
	cd $sysdir
	# Keep st/file_selector on stock libs — do not export green's pango/fontconfig
	./bin/st -q -e "$sysdir/script/file_selector.sh" "PDF Reader" "$mediadir" "pdf"
	retVal=$?
	echo "=================" "$retVal"
	if [ $retVal -eq 137 ] || [ ! -f "$mediadir/.selected.pls" ]; then
		exit
	fi
	selected_file=$(cat "$mediadir/.selected.pls" | tr -d '\r' | head -n 1)
	if [ ! -f "$selected_file" ]; then
		echo "missing selected file: [$selected_file]" >> "$HOME/green-launch.log"
		continue
	fi
	echo "$selected_file"
	# Pango libs + fonts only for green (FAT needs real .so.0 files under greendir/lib)
	FONTCONFIG_FILE="$greendir/fonts.conf" \
	FONTCONFIG_PATH="$greendir" \
	LD_LIBRARY_PATH="$greendir/lib:$sysdir/lib/parasyte:$LD_LIBRARY_PATH" \
		"$greendir/green" "$selected_file"
	echo "green exit $? for $selected_file" >> "$HOME/green-launch.log"
done
