#!/bin/bash
# by Dominik Stanisław Suchora <suchora.dominik7@gmail.com>
# License: GNU GPLv3

# test.sh file.test . FLAGS
# test.sh file.test update FLAGS - update md5 hashes

# test cases do not work on every platform as backslashes are treated differently for some reason, because of that bash has to be used

# tested program has to be in the same directory as this script

### file format
# # a comment
# ! text to be printed
# $ argument appended at the end
# ^ argument appended at the begining
# @ a single name of a test file to be executed
# % flags separated by spaces that have to be defined, if they're not exit
# < FILE processed, only one can be given. It does the same as '$' but prints position relative to caller rather than just command at error
# md5 hash,middle arguments

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

tested_program="reliq"
lastargs=""
firstargs=""
used_file=""

trim_w() {
    sed 's/^[ \t]*//;s/[ \t]*$//'
}

trim_g() {
    trim_w | sed '/^$/!s/^/ /'
}

sed 's/\\/\\\\/g' "$file" | while read i
do
    [ -z "$i" ] && continue
    first="$(echo "$i" | cut -b 1)"

    if echo "$first" | grep -q '^[#@$!%<^]$'
    then
        i_rest="$(echo "$i" | cut -b 2-)"
        case "$first" in
            '@')
                script_file="$(realpath "$(echo "$i_rest" | sed 's/^  *//;s/ .*//')")"
                cd "$previousdir"
                "$script_itself" "$script_file" "$to_update" "$@"
                cd "$currentdir";;
            '%')
                for j in $(echo "$i_rest" | sed 's/  */ /; s/^ //; s/ $//; s/ /\n/g')
                do
                    echo "$*" | sed 's/  */ /; s/^ //; s/ $//; s/ /\n/g' | grep -xqF "$j" || exit
                done;;
            '^') firstargs="$i_rest";;
            '$') lastargs="$i_rest";;
            '!') echo "$i_rest";;
            '<') used_file="$i_rest";;
        esac
        [ -n "$output" ] && echo "$i" >> "$output"
        continue
    fi

    phash="$(echo "$i" | cut -b 1-32)"
    args="$(echo "$i" | cut -b 34-)"
    pcommand="$previousdir/$tested_program $firstargs $args $used_file $lastargs"
    newhash="$(eval "$pcommand" | md5sum | cut -d ' ' -f1)"
    if [ -n "$output" ]
    then
        printf "%s,%s\n" "$newhash" "$args" >> "$output"
    elif [ "$newhash" != "$phash" ]
    then
        t_firstargs="$(echo "$firstargs" | trim_g)"
        t_used_file="$(echo "$used_file" | trim_w)"
        if [ -n "$used_file" ]
        then
            t_used_file="$(realpath "$t_used_file" | awk -v prevdir="$previousdir/" 'BEGIN {prevdirl=length(prevdir);} { if (length($0) < prevdirl) { print " " $0; exit; } r=substr($0,0,prevdirl); if (r != prevdir) print " " $0; else print " " substr($0,prevdirl+1); }')"
        fi
        t_lastargs="$(echo "$lastargs" | trim_g)"
        echo "./$tested_program$t_firstargs $args$t_used_file$t_lastargs - failed"
    fi
done

[ -n "$output" ] && mv -f "$output" "$file"
exit 0
