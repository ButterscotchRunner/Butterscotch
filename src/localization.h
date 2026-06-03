#pragma once

#include <stdio.h>

enum language {
    BS_LANG_ENGLISH
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
