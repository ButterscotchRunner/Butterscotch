#ifndef _BS_STRING_COMPAT_H_
#define _BS_STRING_COMPAT_H_

#include <string.h>
#ifndef NO_STRINGS_H
#include <strings.h>
#endif

#ifdef NO_STRCASECMP

#include <ctype.h>

static int strcasecmp(const char *_s1, const char *_s2) {
#ifdef _WIN32
    return _stricmp(_s1, _s2);
#else
    const unsigned char *s1 = (const unsigned char *)_s1;
    const unsigned char *s2 = (const unsigned char *)_s2;

    while (*s1 && *s2) {
        if (*s1 != *s2 && tolower(*s1) != tolower(*s2))
            break;
        ++s1;
        ++s2;
    }
    return tolower(*s1) - tolower(*s2);
#endif
}

#endif

#ifdef NO_STRTOK_R

static char *strtok_r(char *s, const char *sep, char **p) {
    if (!s && !(s = *p)) return NULL;
    s += strspn(s, sep);
    if (!*s) return *p = 0;
    *p = s + strcspn(s, sep);
    if (**p) *(*p)++ = 0;
    else *p = 0;
    return s;
}

#endif

#endif /* _BS_STRING_COMPAT_H_ */
