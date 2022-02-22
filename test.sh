#!/bin/sh

while read i
do
    h="$(echo "$i" | cut -d ',' -f1)"
    f="$(echo "$i" | cut -d ',' -f2)"
    c="./hgrep $f test.html"
    n="$(eval "$c" | md5sum | cut -d ' ' -f1)"
    [ "$n" != "$h" ] && echo "$c - failed"
done < "$1"
exit 0
