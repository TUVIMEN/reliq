#!/bin/bash

# test.sh file.test . FLAGS
# test.sh file.test update FLAGS - update md5 hashes

# test cases do not work on every platform as backslashes are treated differently for some reason

### file format
# # a comment
# ! text to be printed
# $ argument appended at the end
# @ a single name of a test file to be executed
# % flags separated by spaces that have to be defined, if they're not exit
# md5 hash,first arguments

### paths are relative to test file

script_itself="$(realpath "$0")"
file="$(realpath "$1")"
[ -s "$file" ] || exit

previousdir="$(pwd)"
currentdir="$(dirname "$file")"
cd "$currentdir"

to_update="$2"
[ "$2" = "update" ] && output="$(mktemp)"

shift 2

lastargs=""

sed 's/\\/\\\\/g' "$file" | while read i
do
    [ -z "$i" ] && continue
    first="$(echo "$i" | cut -b 1)"

    if echo "$first" | grep -q '^[#@$!%]$'
    then
        if [ "$first" = '@' ]
        then
            script_file="$(realpath "$(echo "$i" | cut -b 2- | sed 's/^  *//;s/ .*//')")"
            cd "$previousdir"
            "$script_itself" "$script_file" "$to_update" "$@"
            cd "$currentdir"
        fi
        if [ "$first" = '%' ]
        then
            for j in $(echo "$i" | cut -b 2- | sed 's/  */ /; s/^ //; s/ $//; s/ /\n/g')
            do
                echo "$*" | sed 's/  */ /; s/^ //; s/ $//; s/ /\n/g' | grep -xqF "$j" || exit
            done
        fi
        [ "$first" = '$' ] && lastargs="$(echo "$i" | cut -b 2-)"
        [ "$first" = '!' ] && echo "$i" | cut -b 2-
        [ -n "$output" ] && echo "$i" >> "$output"
        continue
    fi

    phash="$(echo "$i" | cut -b 1-32)"
    args="$(echo "$i" | cut -b 34-)"
    pcommand="$previousdir/reliq $args $lastargs"
    newhash="$(eval "$pcommand" | md5sum | cut -d ' ' -f1)"
    if [ -n "$output" ]
    then
        printf "%s,%s\n" "$newhash" "$args" >> "$output"
    else
        [ "$newhash" != "$phash" ] && echo "$pcommand - failed"
    fi
done

[ -n "$output" ] && mv -f "$output" "$file"
exit 0
