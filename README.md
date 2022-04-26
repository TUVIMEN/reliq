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
Get tags 'a' with attribute 'href' at position 0 of value ending with '.org' from result of matching tags 'div' with attribute 'id' and without attribute 'class' from file 'index.html'.

    $ hgrep 'div +id -class; a +[0]href=".*\.org"' index.html

Get tags which does not have any tags inside them from file 'index.html'.

    $ hgrep '.* @M"<"' index.html

Get empty tags from file 'index.html'.

    $ hgrep '.* @s[0]' index.html

Get hyperlinks from level greater or equal to 6 from file 'index.html'.

    $ hgrep 'a +href @l[6-] @p"%(href)a\n"' index.html
