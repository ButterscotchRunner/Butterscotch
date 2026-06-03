#include "localization.h"

#include <string.h>
#include <stdarg.h>

const struct localized_string help_strings_sillylang[] = {
    { "help", "im silly :3" },
    { NULL, NULL }
};

const struct string_category string_categories_sillylang[] = {
    { "help", help_strings_sillylang },
    { NULL, NULL }
};

enum language getLanguage(void) {
    return BS_LANG_ENGLISH;
}

const char *getLocStr(const char *fallback, const char *id) {
    if (!id)
        return fallback;
    const struct string_category *categories;
    switch (getLanguage()) {
        case BS_LANG_SILLY:
            categories = string_categories_sillylang;
            break;
        default:
            return fallback;
    }
    size_t id_dot = 0;
    while (id[id_dot] != '.')
        ++id_dot;
    for (size_t category = 0; categories[category].name; ++category)
        if (strncmp(categories[category].name, id, id_dot) == 0)
            for (size_t i = 0; categories[category].strings[i].name; ++i)
                if (strcmp(categories[category].strings[i].name, id + id_dot + 1) == 0)
                    return categories[category].strings[i].string;

    return fallback;
}

void locLog(const char *fallback, const char *id, ...) {
    va_list ap;
    va_start(ap, id);
    vfprintf(stderr, getLocStr(fallback, id), ap);
    va_end(ap);
}
