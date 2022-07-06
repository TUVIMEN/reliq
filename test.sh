#!/bin/sh

while read i
do
    h="$(echo "$i" | cut -b 1-32)"
    f="$(echo "$i" | cut -b 34-)"
    c="./hgrep $f $2"
    n="$(eval "$c" | md5sum | cut -d ' ' -f1)"
    [ "$n" != "$h" ] && echo "$c - failed"
done < "$1"
exit 0
