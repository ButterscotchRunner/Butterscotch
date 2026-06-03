#pragma once

#include <stdio.h>

enum language {
    LANG_ENGLISH,
    LANG_SILLY,
};

struct localized_string {
    const char *name;
    const char *string;
};

struct string_category {
    const char *name;
    const struct localized_string *strings;
};

const char *getLocStr(const char *fallback, const char *id);
void locLog(const char *fallback, const char *id, ...);
