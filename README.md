# reliq

reliq is a html parsing and searching tool.

## Building

### Build

    make install

### Build library

    make lib-install

### Build linked

    make linked

## Usage

Get some help

    man reliq

Get `div` tags with class `tile`.

    reliq 'div class="tile"'

Get `div` tags with class `tile` as a word and id `current` as a word.

    reliq 'div .tile #current' index.html

Get tags which does not have any tags inside them from file `index.html`.

    reliq '* c@[0]' index.html

Get empty tags from file 'index.html'.

    reliq '* m@>[0]' index.html

Get hyperlinks from level greater or equal to 6 from file `index.html`.

    reliq 'a href @l[6:] | "%(href)v\n"' index.html

Get any tag with class `cont` and without id starting with `img-`.

    reliq '* .cont -#b>img-' index.html

Get hyperlinks ending with `/[0-9]+.html`.

    reliq 'a href=Ee>/[0-9]+\.html | "%(href)a\n"' index.html

Get links that are not at 1 level, both are equivalent.

    reliq 'a href l@[!0] | "(href)v\n"' index.html
    reliq 'a href -l@[0] | "(href)v\n"' index.html

Get `li` tag that does not start with `Views:`, both are equivalent.

    reliq 'li m@bv>"Views:"' index.html
    reliq 'li -m@b>"Views:"' index.html

Get `ul` tags and html inside `i` tags that are inside `p` tags.

    reliq 'ul, p; i | "%i\n"' index.html

Get the last images in every `li` with `id` matching extended regex `img-[0-9]+`.

    reliq 'li #E>img-[0-9]+; img src [-1] | "%(src)v\n"' index.html

Get `tr` and `td` inside `table` tag.

    reliq 'table; { tr, td }' index.html

Get `tr` with either `title` attribute with class `x1` or `x2`, or if it has one child. Note that `)` has to be preceded with space otherwise it will be a part of previous  argument and that brackets have to touch for it to be or.

    reliq 'tr ( title ( .x1 )( .x2 ) )( c@[1] )'

Process output using `cut` for each tag, and with `sed` and `tr` for the whole output.

    reliq 'div #B>msg_[0-9-]* | "%(id)v" cut [2] "-" / sed "s/^msg_//" tr "\n" "\t"' index.html

Process output in a block (note that things in blocks have to be separated by `,` as just newline is not enough).

    reliq '
        {
            div class=B>"memberProfileBanner memberTooltip-header.*" style=a>"url(" | "%(style)v" / sed "s#.*url(##;s#^//#https://#;s/?.*//;p;q" "n",
            img src | "%(src)v" / sed "s/?.*//; q"
        } / sed "s#^//#https://#"
    ' index.html

Get `tr` in `table` with level relative to `table` equal `1`, and process it individually for every tag in block, and at the end of each block delete every `\n` character and append `\n` at the end.

    reliq '
        table border=1; tr l@[1]; {
            td; * c@[0] | "%i\t"
        } | tr "\n" echo "" "\n"
    ' index.html

Same but process all tags at once in block, then process final output of the block deleting all `\n` and appending `\n` at the end. The above creates tsv where each `tr` has its own line, but this example craetes only one line.

    reliq '
        table border=1; tr; {
            td; * c@[0] | "%i\t"
        } / tr "\n" echo "" "\n"
    ' index.html

### JSON like output

Output of expression can be assigned to a field. Fields combined in json like structure will be written at the end of output, if any expression is not assigned to a field it's output will be written before the structure.

    reliq '.links.a a href | "%(href)v\n", img src | "%(src)v\n"'

will return

```
/static/images/icons/wikipedia.png
/static/images/mobile/copyright/wikipedia-wordmark-en.svg
/static/images/mobile/copyright/wikipedia-tagline-en.svg
//upload.wikimedia.org/wikipedia/commons/thumb/0/08/Sodium_nitrite.svg/220px-Sodium_nitrite.svg.png
https//upload.wikimedia.org/wikipedia/commons/thumb/a/a6/Nitrite-3D-vdW.png/110px-Nitrite-3D-vdW.png
https//upload.wikimedia.org/wikipedia/commons/thumb/0/05/Sodium-3D.png/80px-Sodium-3D.png
{"links":["/wiki/Main_Page","/wiki/Wikipedia:Contents","/wiki/Portal:Current_events","/wiki/Special:Random","/wiki/Wikipedia:About"]}
```

Note that from now on any json structure in examples will be prettified for ease of reading, in reality reliq returns compressed json.

It is a json like structure because reliq doesn't enforce json output as in above example, if output would be directly connected to json parser an error might accur when incorrect changes are made to reliq script.

It also does not check for repeating field names e.g.

    reliq '.a span #views | "%i", .a time datetime | "%(datetime)v\a"'

will return incorrect json

```
{
  "a": "29423",
  "a": "2024-07-19 07:12:05"
}
```

So reliq returning json depends on user!

Field is specified when there's `.` followed by string matching `[A-Za-z0-9_-]+` before expression e.g.

    reliq '.some_fiELD-1 p | "%i"'

Fields can only be specified before expression, and will be ignored inside it making incorrect patterns e.g.

    reliq 'ul; .a li' index.html

This will not return json like structure, or anything since `.a` was specified as plain name of tag which cannot exist in html.

Fields take precedence before everything

    reliq '
        .field dd; {
            time datetime | "%(datetime)v\a",
            a m@v>"<" | "%i\a",
            * l@[0] | "%i"
        } / tr '\n' sed "s/^\a*//;s/\a.*//"
    ' index.html

Fields can be nested in another fields

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

will return

```
{
  "user": {
    "name": "TUVIMEN",
    "link": "https://examplesite.com/u/TUVIMEN/",
    "id": 1245
  }
}
```

If field that nests other fields has expression without field, that expression will not be assigned to nesting field, and will output normally before json structure, breaking compability with json e.g.

    reliq '
        .user div #user; {
            .name span .user-name | "%i",
            .link [0] a href | "%(href)v",
            div .user-avatar; img src | "%(src)v\n"
        }
    ' index.html

will return

```
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

    reliq '
        .signature div .signature #B>sig[0-9]* | "%i",
        dl .postprofile #B>profile[0-9]*; {
            dt l@[1]; {
                .avatar img src | "%(src)v",
                .user a c@[0] | "%i",
                .userid.u a href c@[0] | "%(href)v" / sed "s/.*[&;]u=([0-9]+).*/\1/" "E",
            },
            .userinfo.a("\a") dd l@[1] m@vf>"&nbsp;" | "%i\a" / tr '\n\t' sed "
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

will return

```
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

If field is presented with block with `|` with empty format that contains fields, field type will change to array and block will be executed for each tag

    reliq '
        .posts form #quickModForm; table l@[1]; tr l@[1] m@B>"id=\"subject_[0-9]*\"" m@v>".googlesyndication.com/"; {
            .postid.u div #B>subject_[0-9]* | "%(id)v" / sed "s/.*_//",
            .date td valign=middle; div .smalltext | "%i" / sed "s/.* ://;s/^<\/b> //;s/ &#187;//g;s/<br *\/>.*//;s/<[^>]*>//g;s/ *$//",
            .avatar td valign=top rowspan=2; img .avatar src | "%(src)v",
        } | ,
        .title td #top_subject | "%i" / sed "s/[^:]*: //;s/([^(]*)//;s/&nbsp;//g;s/ *$//"
    '

will return

```
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

Note that `,` was separated by space from `|`. Formats can be specified only when `|` or `/` is separated from both sides with whitespaces. You cannot write `li |tr "\n"` or `p/tr "a-z" "A-Z"`, when format is empty that space is still required.

If field name is followed by another `.` with alphanumeric string it will have different type. Types are described in the manual. The most used ones are `.u` and `.a`.

`.u` finds first unsigned integer e.g. on `is -2849.4 kg` will return `2849`.

`.a` splits string by delimiter creating array. By default delimiter is set to `\n` and type to `.s`. So `.a` `.a("\n")` `.a.s` `.a("\n").s` are equivalent. `.a("\t").u` will have `\t` delimiter and will hold element consisting of unsigned integers.

    reliq '.years.a("\t").u table .int-values; [0] tr | "%i" / tr "\n" "\t"'

will return

```
{
    "years": [
        1930,
        1942,
        1948,
        1965,
        1989,
        1994
        2010
    ]
}
```

### Fast mode

Is used when `-F` flag is specified. Causes input to be parsed and processed instantly, without storing it, making it more suitable to processing large files. Because of it any branching with `,` or blocks is not possible and patterns are limited to only being separated by `;` (i.e. chains).

    reliq -F 'tr; a l@[3:]' index.html
    reliq -F 'tr' index.html | reliq -F 'a l@[3:]'

Both of these examples have similar performance.

Because of parsing and processing on the go the order of output might look reversed, that is because the child tags are always processed before the parents, only after the end of tag is found that it may be processed.

## Python interface

[reliq-python](https://github.com/TUVIMEN/reliq-python)

## Projects using reliq

- [forumscraper](https://github.com/TUVIMEN/forumscraper)
- [torge](https://github.com/TUVIMEN/torge)
- [wordpress-madara-scraper](https://github.com/TUVIMEN/wordpress-madara-scraper)
- [stackexchange-scraper](https://github.com/TUVIMEN/stackexchange-scraper)
- [elektroda-scraper](https://github.com/TUVIMEN/elektroda-scraper)
- and many others of my repositories
