#!/bin/sh
sysdir=/mnt/SDCARD/.tmp_update
mediadir=/mnt/SDCARD/Media/PDF
greendir=$sysdir/bin/green
export HOME=/mnt/SDCARD/Saves/CurrentProfile/saves/green
mkdir -p "$HOME" "$HOME/fontconfig-cache"
export FONTCONFIG_FILE="$greendir/fonts.conf"
export FONTCONFIG_PATH="$greendir"
# greendir/lib first: FAT has no symlinks; real lib*.so.0 copies live here
export LD_LIBRARY_PATH="$greendir/lib:$sysdir/lib/parasyte:$LD_LIBRARY_PATH"

while :
do
	cd $sysdir
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
	"$greendir/green" "$selected_file"
	echo "green exit $? for $selected_file" >> "$HOME/green-launch.log"
done