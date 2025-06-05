# reliq

HTML parser with it's own matching language. Benchmark [here](https://github.com/TUVIMEN/reliq-python#benchmark), examples of syntax with highlighting [here](https://github.com/TUVIMEN/reliq-vim).

## Installation

### AUR

```shell
yay -S reliq
```

### CLI tool

```shell
make install
```

### Library only

```shell
make lib-install
```

### Linked CLI tool and library

```shell
make linked
```

## Build options

```makefile
D := 0 # debug build
S := 0 # sanitizer build

O_SMALL_STACK := 0 # change internal limits for a small stack
O_PHPTAGS := 1 # support for <?php ?>
O_AUTOCLOSING := 1 # html quirks, can be disabled if you close your tag correctly

# only one should be set
O_HTML_FULL := 0 # full html sizes, you probably don't know what you're doing
O_HTML_SMALL := 1 # reasonable html sizes
O_HTML_VERY_SMALL := 0 # this can be set if you don't put thousands of spaces between attributes
```

## Usage

Manual provides coarse, but complete documentation about the tool and expression language.

```shell
man reliq
```

I recommend reading it with colors like

```bash
man() {
    LESS_TERMCAP_md=$'\e[01;31m' \
    LESS_TERMCAP_me=$'\e[0m' \
    LESS_TERMCAP_se=$'\e[0m' \
    LESS_TERMCAP_so=$'\e[01;32;31m' \
    LESS_TERMCAP_ue=$'\e[0m' \
    LESS_TERMCAP_us=$'\e[01;34m' \
    command man "$@"
}
```

Get `div` tags with class `tile`.

```shell
reliq 'div class="tile"'
```

Get `div` tags with class `tile` as a word and id `current` as a word.

```shell
reliq 'div .tile #current' index.html
```

Get tags which does not have any tags inside them from file `index.html`.

```shell
reliq '* c@[0]' index.html
```

Get tags that have length of their insides equal to 0 from file 'index.html'.

```shell
reliq '* i@>[0]' index.html
```

Get hyperlinks from level greater or equal to 6 from file `index.html`.

```shell
reliq 'a href @l[6:] | "%(href)v\n"' index.html
```

Get any tag with class `cont` and without id starting with `img-`.

```shell
reliq '* .cont -#b>img-' index.html
```

Get hyperlinks ending with `/[0-9]+\.html`.

```shell
reliq 'a href=Ee>/[0-9]+\.html | "%(href)a\n"' index.html
```

Get links that are not at 1 level, both are equivalent.

```shell
reliq 'a href l@[!0] | "(href)v\n"' index.html
reliq 'a href -l@[0] | "(href)v\n"' index.html
```

Get `li` tags of which insides don't start with `Views:`, both are equivalent.

```shell
reliq 'li i@bv>"Views:"' index.html
reliq 'li -i@b>"Views:"' index.html
```

Get `ul` tags and html inside `i` tags that are inside `p` tags.

```shell
reliq 'ul, p; i | "%i\n"' index.html
```

Get the second to last image in every `li` with `id` matching extended regex `img-[0-9]+`.

```shell
reliq 'li #E>img-[0-9]+; img src [-1] | "%(src)v\n"' index.html
```

Get `tr` and `td` inside `table` tag.

```shell
reliq 'table; { tr, td }' index.html
```

Get `tr` with either `title` attribute with class `x1` or `x2`, or if it has one child. Note that `)` has to be preceded with space otherwise it will be a part of previous  argument and that brackets have to touch for it to be or.

```shell
reliq 'tr ( title ( .x1 )( .x2 ) )( c@[1] )'
```

Process output using `cut` for each tag, and with `sed` and `tr` for the whole output.

```shell
reliq 'div #B>msg_[0-9-]* | "%(id)v" cut [1] "-" / sed "s/^msg_//" tr "\n" "\t"' index.html
```

Process output in a block (note that things in blocks have to be separated by `,` as just newline is not enough).

```shell
reliq '
    {
        div class=B>"memberProfileBanner memberTooltip-header.*" style=a>"url(" | "%(style)v" / sed "s#.*url(##;s#^//#https://#;s/?.*//;p;q" "n",
        img src | "%(src)v" / sed "s/?.*//; q"
    } / sed "s#^//#https://#"
' index.html
```

Get `tr` in `table` with level relative to `table` equal `1`, and process it individually for every tag in block, and at the end of each block delete every `\n` character and append `\n` at the end.

```shell
reliq '
    table border=1; tr l@[1]; {
        td; * c@[0] | "%i\t"
    } | tr "\n" echo "" "\n"
' index.html
```

Same but process all tags at once in block, then process final output of the block deleting all `\n` and appending `\n` at the end. The above creates tsv where each `tr` has its own line, but this example creates only one line.

```shell
reliq '
    table border=1; tr; {
        td; * c@[0] | "%i\t"
    } / tr "\n" echo "" "\n"
' index.html
```

### JSON like output

Output of expression can be assigned to a field. Fields combined in json like structure will be written at the end of output, if any expression is not assigned to a field it's output will be written before the structure.

```shell
reliq '.links.a a href | "%(href)v\n", img src | "%(src)v\n"'
```

will return

```json
/static/images/icons/wikipedia.png
/static/images/mobile/copyright/wikipedia-wordmark-en.svg
/static/images/mobile/copyright/wikipedia-tagline-en.svg
//upload.wikimedia.org/wikipedia/commons/thumb/0/08/Sodium_nitrite.svg/220px-Sodium_nitrite.svg.png
https//upload.wikimedia.org/wikipedia/commons/thumb/a/a6/Nitrite-3D-vdW.png/110px-Nitrite-3D-vdW.png
https//upload.wikimedia.org/wikipedia/commons/thumb/0/05/Sodium-3D.png/80px-Sodium-3D.png
{"links":["/wiki/Main_Page","/wiki/Wikipedia:Contents","/wiki/Portal:Current_events","/wiki/Special:Random","/wiki/Wikipedia:About"]}
```

Note that from now on any json structure in examples will be prettified for ease of reading, in reality reliq returns compressed json.

It is a json like structure because reliq doesn't enforce json output as in above example, if output would be directly connected to json parser an error might occur when incorrect changes are made to reliq script.

It also does not check for repeating field names e.g.

```shell
reliq '
    .a span #views | "%i",
    .a time datetime | "%(datetime)v\a"
'
```

will return incorrect json

```json
{
  "a": "29423",
  "a": "2024-07-19 07:12:05"
}
```

So reliq returning json depends on user!

Field is specified when there's `.` followed by string matching `[A-Za-z0-9_-]+` before expression e.g.

```shell
reliq '.some_fiELD-1 p | "%i"'
```

Fields can only be specified before expression, and will be ignored inside it making incorrect patterns e.g.

```shell
reliq 'ul; .a li' index.html
```

This will not return json like structure, or anything since `.a` was specified as plain name of tag which cannot exist in html.

Fields take precedence before everything

```shell
reliq '
    .field dd; {
        time datetime | "%(datetime)v\a",
        a i@v>"<" | "%i\a",
        * l@[0] | "%i"
    } / tr '\n' sed "s/^\a*//;s/\a.*//"
' index.html
```

Fields can be nested in another fields

```shell
reliq '
    .user {
        h4 class=b>message-name,
        * .MessageCard__user-info__name
    }; {
        .name [0] * c@[0] | "%i",
        .link [0] a class href | "%(href)v",
        .id.u [0] * data-user-id | "%(data-user-id)v"
    },
' index.html
```

will return

```json
{
  "user": {
    "name": "TUVIMEN",
    "link": "https://examplesite.com/u/TUVIMEN/",
    "id": 1245
  }
}
```

If field that nests other fields has expression without field, that expression will not be assigned to nesting field, and will output normally before json structure, breaking compability with json e.g.

```shell
reliq '
    .user div #user; {
        .name span .user-name | "%i",
        .link [0] a href | "%(href)v",
        div .user-avatar; img src | "%(src)v\n"
    }
' index.html
```

will return

```json
https://exampleother.com/a/8122.jpg
{
    "user": {
        "name": "hexderm",
        "link": "https://exampleother.com/users/8122"
    }
}
```

This further underlines the importance of writing correct script.

Unless fields are nested they will be on the same level no matter where they are in script

```shell
reliq '
    .signature div .signature #B>sig[0-9]* | "%i",
    dl .postprofile #B>profile[0-9]*; {
        dt l@[1]; {
            .avatar img src | "%(src)v",
            .user a c@[0] | "%i",
            .userid.u a href c@[0] | "%(href)v" / sed "s/.*[&;]u=([0-9]+).*/\1/" "E",
        },
        .userinfo.a("\a") dd l@[1] i@vf>"&nbsp;" | "%i\a" / tr '\n\t' sed "
            s/<strong>([^<]*)<\/strong>/\1/g
            s/ +:/:/
            /<ul [^>]*class=\"profile-icons\">/{
                s/.*<a href=\"([^\"]*)\" title=\"Site [^\"]*\".*/Site\t\1/
                t;d
            }
            /^[^<>]+:/!{s/^/Rank:/}
            s/: */\t/
        " "E" "\a"
    }
' index.html
```

will return

```json
{
  "signature": "Helicopter: \"helico\" -> spiral, \"pter\" -> with wings",
  "avatar": "https://other.org/as/d.png",
  "user": "Twospoons",
  "userid": 5218,
  "userinfo": [
    "Posts\t1306",
    "Mood\tA trace of hope..."
  ]
}
```

If field has block with fields that has `|` while format is empty, field type will change to array and block will be executed for each tag

```shell
reliq '
    .posts form #quickModForm; table l@[1]; tr l@[1] i@B>"id=\"subject_[0-9]*\"" i@v>".googlesyndication.com/"; {
        .postid.u div #B>subject_[0-9]* | "%(id)v" / sed "s/.*_//",
        .date td valign=middle; div .smalltext | "%i" / sed "s/.* ://;s/^<\/b> //;s/ &#187;//g;s/<br *\/>.*//;s/<[^>]*>//g;s/ *$//",
        .avatar td valign=top rowspan=2; img .avatar src | "%(src)v",
    } |,
    .title td #top_subject | "%i" / sed "s/[^:]*: //;s/([^(]*)//;s/&nbsp;//g;s/ *$//"
'
```

will return

```json
{
    "posts": [
        {
          "postid": 62225223,
          "date": "May 10, 2023, 08:46:51 PM",
          "avatar": "/useravatars/avatar_2654005.png"
        },
        {
          "postid": 62225251,
          "date": "May 10, 2023, 08:57:28 PM",
          "avatar": "/useravatars/avatar_300014.png"
        }
    ],
    "title": "Bitcoins held by the US government"
}
```

If field name is followed by another `.` with alphanumeric string it will have different type. Types are described in the manual. The most used ones are `.u` and `.a`.

`.u` finds first unsigned integer e.g. on `is -2849.4 kg` will return `2849`.

`.a` splits string by delimiter creating array. By default delimiter is set to `\n` and type to `.s`. So `.a` `.a("\n")` `.a.s` `.a("\n").s` are equivalent. `.a("\t").u` will have `\t` delimiter and will hold element consisting of unsigned integers.

```shell
reliq '.years.a("\t").u table .int-values; [0] tr | "%i" / tr "\n" "\t"'
```

will return

```json
{
    "years": [
        1930,
        1942,
        1948,
        1965,
        1989,
        1994,
        2010
    ]
}
```

## Python interface

[reliq-python](https://github.com/TUVIMEN/reliq-python)

## Projects using reliq

- [forumscraper](https://github.com/TUVIMEN/forumscraper)
- [torge](https://github.com/TUVIMEN/torge)
- [wordpress-madara-scraper](https://github.com/TUVIMEN/wordpress-madara-scraper)
- [mangabuddy-scraper](https://github.com/TUVIMEN/mangabuddy-scraper)
- [elektroda-scraper](https://github.com/TUVIMEN/elektroda-scraper)
- [lightnovelworld](https://github.com/TUVIMEN/lightnovelworld)
- and many others of my repositories

## Syntax highlighting

- [vim](https://github.com/TUVIMEN/reliq-vim)
