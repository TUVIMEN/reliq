#!/bin/sh

[ "$3" = "update" ] && output="$(mktemp)"

sed 's/\\/\\\\/g' "$1" | while read i
do
    h="$(echo "$i" | cut -b 1-32)"
    f="$(echo "$i" | cut -b 34-)"
    c="./hgrep $f $2"
    n="$(eval "$c" | md5sum | cut -d ' ' -f1)"
    if [ -n "$output" ]
    then
        printf "%s,%s\n" "$n" "$f" >> "$output"
    else
        [ "$n" != "$h" ] && echo "$c - failed"
    fi
done

[ -n "$output" ] && mv -f "$output" "$1"
exit 0
