#ifndef CTYPE_H
#define CTYPE_H

extern const char IS_ALNUM[];
extern const char IS_ALPHA[];
extern const char IS_DIGIT[];
extern const char IS_SPACE[];

#define isalnum(x) IS_ALNUM[(unsigned char)x]
#define isalpha(x) IS_ALPHA[(unsigned char)x]
#define isdigit(x) IS_DIGIT[(unsigned char)x]
#define isspace(x) IS_SPACE[(unsigned char)x]

#endif

