#!/bin/bash
CFLAGS="${CFLAGS:--O0 -g -Wall}"
if [[ "x$1" == "xclean" ]]; then
	rm -rf bin
	rm -f .build
	exit 0
fi
last_checksums="$(cat .build 2>/dev/null)"
getsum() {
	for i in $last_checksums; do
		cs="${i#$1:}"
		if [[ "$1:$cs" == "$i" ]]; then
			echo "$cs"
			return 0
		fi
	done
	return 1
}
dosum() {
	sha1sum $1 | sed 's/^\([0-9a-f]*\)\s\+.*$/\1/'
}

mkdir -p bin
truncate --size 0 .build

common_sources="$(echo utils/*.c xc-*.c)"
common_headers="$(echo utils/*.h xc-*.h)"
for f in $common_sources $common_headers; do
	sum=$(dosum $f)
	if [[ "x$(getsum $f)" != "x$sum" ]]; then
		rm -f bin/*
	fi
	echo "$f:$sum" >> .build
done

build=""
for file in sc-*.c; do
	sum=$(dosum $file)
	echo "$file:$sum" >> .build
	app="bin/${file%.c}"
	asum=
	if [[ -e "$app" ]]; then
		asum=$(dosum $app)
	fi
	[[ "x$(getsum $file)" == "x$sum" ]] && [[ -e "$app" ]] && [[ "x$(getsum $app)" == "x$asum" ]] && { echo "$app:$asum" >> .build; continue; }
	build="${build:+$build }$file"
done

for file in $build; do
	app="bin/${file%.c}"
	echo -e ">>> \e[0;34m$file\e[1;0m -> \e[0;32m$app\e[1;0m"
	rm -f $app
	gcc $CFLAGS -I. $(pkg-config --cflags --libs xmms2-client xmms2-client-glib glib-2.0) -o "$app" $common_sources "$file" || exit 1
	echo "$app:$(dosum $app)" >> .build
done
