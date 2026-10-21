#include "native.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void help(void) {
    puts("Usage: butterscotch-preprocessor [<options>] <datawinpath>\n"
         "\nOptions:\n"
         "  -o, --output=<path>     Output directory (default: output)\n"
         "  --target=(ps2|ps3)      Output platform (default: ps2)\n"
         "  --atlas-size=<pixels>   Square atlas dimensions (default: 512; PS2)\n"
         "  --force-4bpp=<regex>    Full internal image-name PCRE2 match; repeatable (PS2)\n"
         "  --debug-output=<path>   Extract debug PNGs (PS2)\n"
         "  -h, --help              Show help\n"
         "\nImage names include spr/, bg/, font/, or tile/ prefixes.");
}

int main(int argc, char **argv) {
    Options o = {.output = "output", .atlas_size = 512};
    bool positional = false;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!positional && !strcmp(arg, "--")) { positional = true; continue; }
        if (!positional && (!strcmp(arg, "--help") || !strcmp(arg, "-h"))) {
            help(); free(o.patterns); return 0;
        }
        if (!positional && arg[0] == '-' && arg[1]) {
            const char *equal = strchr(arg, '=');
            size_t n = equal ? (size_t)(equal - arg) : strlen(arg);
            const char *value = equal ? equal + 1 : NULL;
            bool attached_output = !equal && n>2 && arg[0]=='-' && arg[1]=='o';
            bool output = attached_output || (n == 2 && !strncmp(arg, "-o", n)) || (n == 8 && !strncmp(arg, "--output", n));
            bool target = n == 8 && !strncmp(arg, "--target", n);
            bool atlas = n == 12 && !strncmp(arg, "--atlas-size", n);
            bool pattern = n == 12 && !strncmp(arg, "--force-4bpp", n);
            bool debug = n == 14 && !strncmp(arg, "--debug-output", n);
            if (!output && !target && !atlas && !pattern && !debug) fail("unknown option '%s'", arg);
            if (attached_output) value=arg+2;
            if (!value) {
                if (++i >= argc) fail("missing value for '%s'", arg);
                value = argv[i];
            }
            if (output) o.output = value;
            else if (debug) o.debug = value;
            else if (target) {
                if (strcmp(value, "ps2") && strcmp(value, "ps3")) fail("target must be ps2 or ps3");
                o.ps3 = !strcmp(value, "ps3");
            } else if (atlas) {
                errno = 0;
                char *end;
                long size = strtol(value, &end, 10);
                if (errno || !*value || *end || size < 1 || size > UINT16_MAX)
                    fail("atlas size must be an integer from 1 to 65535");
                o.atlas_size = (int)size;
            } else {
                o.patterns = resize_array(o.patterns, o.pattern_count + 1, sizeof *o.patterns);
                o.patterns[o.pattern_count++] = (char *)value;
            }
        } else {
            if (o.input) fail("only one input data.win may be specified");
            o.input = arg;
        }
    }
    if (!o.input) fail("missing data.win path (use --help for usage)");
    struct stat st;
    if (stat(o.input, &st)) fail("cannot access '%s': %s", o.input, strerror(errno));
    if (!S_ISREG(st.st_mode)) fail("input '%s' is not a regular file", o.input);
    process(&o);
    free(o.patterns);
    return 0;
}
