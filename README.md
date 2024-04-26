# reliq

reliq is a html parsing and searching tool.

## Building

### Build

    make install

### Build library

    make lib-install

### Build linked

    make linked

## Examples

Get 'div' tags with class 'tile'.

    reliq 'div class="tile"'

Get 'div' tags with class 'tile' as a word and id 'current' as a word.

    reliq 'div .tile #current' index.html

Get tags which does not have any tags inside them from file 'index.html'.

    reliq '* c@[0]' index.html

Get empty tags from file 'index.html'.

    reliq '* m@>[0]' index.html

Get hyperlinks from level greater or equal to 6 from file 'index.html'.

    reliq 'a href @l[6:] | "%(href)v\n"' index.html

Get any tag with class "cont" and without id starting with "img-".

    reliq '* .cont -#b>img-' index.html

Get hyperlinks ending with /[0-9]+.html

    reliq 'a href=Ee>/[0-9]+\.html | "%(href)a\n"' index.html

Get 'ul' tags and then 'i' tags inside 'p'.

    reliq 'ul, i; p' index.html

Get the last images in every 'li' with "img-[0-9]+" id

    reliq 'li #E>img-[0-9]+; img src [-1] | "%(src)v\n"' index.html

Get 'tr' and 'td' inside 'table' tag.

    reliq 'table; { tr, td }' index.html

Get a tsv list of us presidents

    curl 'https://www.loc.gov/rr/print/list/057_chron.html' | ./reliq 'table border=1; tr; { td; * c@[0] | "%i\t" } | tr "\n" echo "" "\n"'

Get some help

    man reliq
