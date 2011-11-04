#!/bin/bash
CFLAGS="${CFLAGS:--O0 -g -Wall}"
rm -rf bin
[[ "x$1" == "xclean" ]] && exit 0
mkdir -p bin
for file in sc-*.c; do
  app="bin/${file%.c}"
  echo -e ">>> \e[0;34m$file\e[1;0m -> \e[0;32m$app\e[1;0m"
  gcc $CFLAGS -I. $(pkg-config --cflags --libs xmms2-client xmms2-client-glib glib-2.0) -o "$app" utils/*.c xc-main.c "$file"
done
