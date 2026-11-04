#!/bin/sh
sysdir=/mnt/SDCARD/.tmp_update
mediadir=/mnt/SDCARD/Media/PDF
greendir=$sysdir/bin/green
export HOME=/mnt/SDCARD/Saves/CurrentProfile/saves/green
mkdir -p "$HOME" "$HOME/fontconfig-cache"
# Fonts for Arabic TOC (Pango/fontconfig); DejaVuSans lives under greendir/fonts
export FONTCONFIG_FILE="$greendir/fonts.conf"
export FONTCONFIG_PATH="$greendir"
# Bundled pango/harfbuzz first (FAT-safe real .so copies), then Parasyte
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
	# Strip CR so Windows-edited .pls paths still work
	selected_file=$(tr -d '\r' < "$mediadir/.selected.pls")
	if [ ! -f "$selected_file" ]; then
		continue
	fi
	echo "$selected_file"
	"$greendir/green" "$selected_file"
	green_ret=$?
	echo "$(date) file=$selected_file exit=$green_ret" >> "$HOME/green-launch.log"
done
