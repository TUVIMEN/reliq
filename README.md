# hgrep
hgrep is a simple html parsing tool.

## Building
    make

## Installation
    make install

## Examples
Get tags 'a' with attribute 'href' at position 0 of value ending with '.org' from result of matching tags 'div' with attribute 'id' and without attribute 'class' from file 'index.html'.

    $ hgrep 'div +id -class; a +[0]href=".*\\.org"' index.html

Get tags which does not have any tags inside them from file 'index.html'.

    $ hgrep '.* /!/</' index.html

Get empty tags from file 'index.html'.

    $ hgrep '.* /[\-][\-][0]' index.html

Get hyperlinks from level greater or equal to 6 from file 'index.html'.

    $ hgrep 'a +href /[\-][6\-]' \-printf '%(href)a\\\\n' index.html
