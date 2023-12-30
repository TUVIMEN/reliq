# hgrep

hgrep is a simple html parsing tool.

## Building

### Build

    make install

### Build library

    make lib-install

### Build linked

    make linked

## Examples

Get 'div' tags with class 'tile'.

    $ hgrep 'div class="tile"'

Get 'div' tags with class 'tile' as a word and id 'current' as a word.

    $ hgrep 'div .tile #current' index.html

Get tags which does not have any tags inside them from file 'index.html'.

    $ hgrep '.* @M"<"' index.html

Get empty tags from file 'index.html'.

    $ hgrep '.* @s[0]' index.html

Get hyperlinks from level greater or equal to 6 from file 'index.html'.

    $ hgrep 'a +href @l[6:] | "%(href)a\n"' index.html

Get any tag with class "cont" and without id starting with "img-".

    $ hgrep '.* .cont -#img-.*' index.html

Get 'ul' tags and then 'i' tags inside 'p'.

    $ hgrep 'ul, i; p' index.html

Get 'tr' and 'td' inside 'table' tag.

    $ hgrep 'table; { tr, td }' index.html
