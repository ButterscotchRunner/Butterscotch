#include "localization.h"

#include <string.h>
#include <stdarg.h>

/*
 * To add a language, create a category array and one or more string arrays.
 * Then, add your language to the switch statement in getLocStr().
 *
 * Below is a language called 'sillylang' that exists to act as an example
 * of how you would add a language. It should be removed once at least one
 * real language is added to this file.
 *
 * The only string sillylang translates is the description for the --help
 * option in the --help usage message, it translates 'Show this message'
 * to 'im silly :3'.
 *
 * If a string is unhandled by your translation, it will show up as the
 * fallback string passed to getLocStr(). This will be an English version
 * of the text.
 */

/*
const struct localized_string help_strings_sillylang[] = {
    { "help", "im silly :3" },
    { NULL, NULL }
};

const struct string_category string_categories_sillylang[] = {
    { "help", help_strings_sillylang },
    { NULL, NULL }
};
*/

static enum language getLanguage(void) {
    /*
     * TODO: add more languages
     * Also prolly don't fetch it again every time cuz this
     * function might get called frequently, so have a static
     * variable that gets inited once and subsequent calls
     * can return the cached value.
     */
    return BS_LANG_ENGLISH;
}

const char *getLocStr(const char *fallback, const char *id) {
    if (!id)
        return fallback;
    const struct string_category *categories;
    switch (getLanguage()) {
    /*
        case BS_LANG_SILLY:
            categories = string_categories_sillylang;
            break;
    */
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
