/*
    reliq - html searching tool
    Copyright (C) 2020-2025 Dominik Stanisław Suchora <hexderm@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "../ext.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdbool.h>

extern FILE *errfile;

bool should_colorize(FILE *o);

static void
usage_color(FILE *o, char *color, const char *s, ...)
{
  if (color) {
    fputs("\033[",o);
    fputs(color,o);
    fputc('m',o);
  }

  va_list ap;
  va_start(ap,s);
  vfprintf(errfile,s,ap);
  va_end(ap);

  if (color)
    fputs("\033[0m",o);
}

#define color(x,...) usage_color(o,cancolor ? (x) : NULL,__VA_ARGS__)
#define COLOR_OPTION "35;1"
#define COLOR_ARG "36"
#define COLOR_SCRIPT "32"
#define COLOR_INPUT "33"
#define COLOR_SECTION "34;1"
#define COLOR_CHAR_STR "31"
#define COLOR_CHAR_ESCAPE "35"
#define COLOR_HIGHLIGHT "36;1"

static void
usage_color_option(FILE *o, const bool cancolor, const char *shortopt, const char *longopt, const char *arg)
{
  fputs("  ",o);
  if (shortopt) {
    fputc('-',o);
    color(COLOR_OPTION,shortopt);
    if (longopt)
      fputs(", ",o);
  }
  if (longopt) {
    fputs("--",o);
    color(COLOR_OPTION,longopt);
  }
  if (arg != NULL) {
    fputc(' ',o);
    color(COLOR_ARG,arg);
  }
}
#define color_option(x,y,z) usage_color_option(o,cancolor,x,y,z)

static void
usage_end_default(FILE *o, const bool cancolor, const char *value)
{
  if (value != (void*)1)
    fputc(' ',o);
  fputc('(',o);
  if ((void*)value > (void*)1) {
    color(COLOR_HIGHLIGHT,"by default");
    fputc(' ',o);
    color(COLOR_ARG,"%s",value);
  } else {
    color(COLOR_HIGHLIGHT,"default");
  }
  fputs(")\n",o);
}

#define end_default(x) usage_end_default(o,cancolor,x)

static void
usage_bool(FILE *o, const bool cancolor, const char *name, const char *desc, const char *d_other)
{
  color_option(NULL,name,NULL);
  fputs(desc,o);
  if (!d_other) end_default(NULL); else fputc('\n',o);

  char negation[256] = "no-";
  strncat(negation,name,252);

  color_option(NULL,negation,NULL);
  if (d_other) {
    fputs(d_other ? d_other : "",o);
    end_default((void*)1);
  } else fputc('\n',o);
  fputc('\n',o);
}
#define bool_opt(x,y,z) usage_bool(o,cancolor,x,y,z)

void
usage(const char *argv0, FILE *o)
{
  if (!o)
    return;
  bool cancolor = should_colorize(o);

  color(COLOR_SECTION,"Usage");
  fprintf(o,": %s [",argv0);
  color(COLOR_OPTION,"OPTION");
  fputs("]... ",o);
  color(COLOR_SCRIPT,"PATTERNS");
  fputs(" [",o);
  color(COLOR_INPUT,"FILE");
  fputs("]...\n",o);

  fputs("Search for ",o);
  color(COLOR_SCRIPT,"PATTERNS");
  fputs(" in each html ",o);
  color(COLOR_INPUT,"FILE");
  fputs(".\n\n",o);

  color(COLOR_SECTION,"Example");
  fputs(": ",o);
  fputs(argv0,o);
  fputs(" \'",o);
  color(COLOR_SCRIPT,"div id; a href=e>\".org\"");
  fputs("' ",o);
  color(COLOR_INPUT,"index.html");
  fputs("\n\n",o);

  color(COLOR_SECTION,"General");
  fputs(":\n",o);

  color_option("h","help",NULL);
  fputs("\t\t\tshow help\n",o);

  color_option("v","version",NULL);
  fputs("\t\t\tshow version\n",o);

  color_option("r","recursive",NULL);
  fputs("\t\tread all files under each directory, recursively\n",o);

  color_option("R","dereference-recursive",NULL);
  fputs("\tlikewise but follow all symlinks\n",o);

  color_option("o","output","FILE");
  fputs("\t\tchange output to a ",o);
  color(COLOR_ARG,"FILE");
  fputs(" instead of ",o);
  color(COLOR_ARG,"stdout");
  fputc('\n',o);

  color_option("E","error-file","FILE");
  fputs("\t\tchange output of errors to a ",o);
  color(COLOR_ARG,"FILE");
  fputs(" instead of ",o);
  color(COLOR_ARG,"stderr");
  fputc('\n',o);

  fputs("\nFollowing options can be treated as subcommands that change mode of operation,\nuse of suboption that is unique to subcommand will implicitly change mode.\n",o);

  fputs("\n--",o);
  color(COLOR_SECTION,"html");
  fputs(": process html, first argument is treated as ",o);
  color(COLOR_SCRIPT,"PATTERNS");
  fputs(" unless -",o);
  color(COLOR_OPTION,"f");
  fputs(" or -",o);
  color(COLOR_OPTION,"e");
  fputs(" options are set",o);
  end_default(NULL);

  color_option("l","list-structure",NULL);
  fputs("\t\tlist structure of ",o);
  color(COLOR_INPUT,"FILE");
  fputc('\n',o);

  color_option("e","expression","PATTERNS");
  fputs("\tuse ",o);
  color(COLOR_ARG,"PATTERNS");
  fputs(" instead of first input\n",o);

  color_option("f","file","FILE");
  fputs("\t\tobtain ",o);
  color(COLOR_SCRIPT,"PATTERNS");
  fputs(" from ",o);
  color(COLOR_ARG,"FILE");
  fputc('\n',o);

  color_option("u","url","URL");
  fputs("\t\t\tset url reference for joining",o);
  fputc('\n',o);

  fputs("\n--",o);
  color(COLOR_SECTION,"urljoin");
  fputs(": join urls passed as arguments with first url passed\n",o);

  fputs("\n--",o);
  color(COLOR_SECTION,"encode");
  fputs(": encode ",o);
  color(COLOR_CHAR_STR,"'&'");
  fputs(", ",o);
  color(COLOR_CHAR_STR,"'<'");
  fputs(", ",o);
  color(COLOR_CHAR_STR,"'>'");
  fputs(", ",o);
  color(COLOR_CHAR_STR,"'\"'");
  fputs(", ",o);
  color(COLOR_CHAR_STR,"'");
  color(COLOR_CHAR_ESCAPE,"\\'");
  color(COLOR_CHAR_STR,"'");
  fputs(" to html entities\n",o);

  fputs("\n--",o);
  color(COLOR_SECTION,"encode-full");
  fputs(": encode all possible characters to html entities\n",o);

  fputs("\n--",o);
  color(COLOR_SECTION,"decode");
  fputs(": decode html entities, while translating &nbsp; to space\n",o);

  fputs("\n--",o);
  color(COLOR_SECTION,"decode-exact");
  fputs(": decode html entities\n",o);

  fputs("\n-",o);
  color(COLOR_SECTION,"p");
  fputs(", --",o);
  color(COLOR_SECTION,"pretty");
  fputs(": prettify html (defaults are set only if this option is set)\n",o);

  color_option("L","maxline","UINT");
  fputs("\t\tmax width of text in block excluding indentation, if set to 0 output is minified",o);
  end_default("90");
  color_option(NULL,"indent","UINT");
  fputs("\t\t\tset indentation width",o);
  end_default("2");
  color_option(NULL,"cycle-indent","UINT");
  fputs("\t\tif number of indentations is exceeded it's reset to 0 (\fIby default 0\fR)",o);
  end_default("0");
  fputc('\n',o);

  bool_opt("wrap-script","\t\t\twrap contents of script tag",0);
  bool_opt("wrap-style","\t\t\twrap contents of style tag",0);
  bool_opt("wrap-text","\t\t\twrap text nodes",0);
  bool_opt("wrap-comments","\t\twrap insides of comment nodes",0);

  color_option(NULL,"color",NULL);
  fputs("\t\t\tcolorize output if in terminal",o);
  end_default(NULL);
  color_option(NULL,"force-color",NULL);
  fputs("\t\t\talways colorize output\n",o);
  color_option(NULL,"no-color",NULL);
  fputs("\n\n",o);

  bool_opt("trim-tags","\t\t\ttrim whitespaces in tags insides",NULL);
  bool_opt("trim-attribs","\t\ttrim whitespaces inbetween and in attribute values",NULL);
  bool_opt("trim-comments","\t\ttrim whitespaces in comments beginning and ending",NULL);
  bool_opt("normal-case","\t\t\tmake tag names, attribute names, classes and ids lowercase",NULL);
  bool_opt("trim-comments","\t\ttrim whitespaces in comments beginning and ending",NULL);
  bool_opt("fix","\t\t\t\tadd missing ending tags and match it's case to starting tag",NULL);
  bool_opt("order-attribs","\t\torder repeating atributes in the same tag",NULL);
  bool_opt("remove-comments","\t\tremove all comment nodes","\t\t");
  bool_opt("overlap-ending","\t\tAllow other tags/comments/text after ending of tag or comments unless both of them fit in maxline limit","\t\t");

  fputs("When input files aren't specified, standard input will be read.\n",o);

  exit(1);
}
