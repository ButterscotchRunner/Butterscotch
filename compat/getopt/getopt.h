#ifndef _BS_GETOPT_H_
#define _BS_GETOPT_H_

/*
 * Minimal header-only getopt_long — long options only, no short opts.
 * Supports permutation: non-option arguments are rotated to the end of
 * argv on each call, so options are found regardless of their position.
 * After the loop, argv[optind..argc-1] holds all non-option arguments
 * in their original relative order.
 *
 * All state is static — safe to include in multiple translation units
 * without duplicate-symbol errors.
 *
 * Set opterr = 0 to suppress error messages.
 *
 * Usage:
 *   while ((c = getopt_long(argc, argv, "", long_opts, NULL)) != -1) { ... }
 */

#include <stdio.h>
#include <string.h>

struct option {
    const char *name;
    int         has_arg;   /* no_argument=0, required_argument=1, optional_argument=2 */
    int        *flag;      /* if non-NULL, *flag = val and return 0; else return val */
    int         val;
};

enum { no_argument = 0, required_argument = 1, optional_argument = 2 };

static int   optind = 1;
static int   opterr = 1;
static int   optopt = 0;
static char *optarg = NULL;

/* Reverse argv[a..b) in place. */
static inline void _gl_rev(char **v, int a, int b) {
    for (char *t; a < --b; ++a) { t = v[a]; v[a] = v[b]; v[b] = t; }
}

/* Rotate argv[l..r) left by n: moves the first n elements to the end. */
static inline void _gl_rotate(char **v, int l, int r, int n) {
    _gl_rev(v, l, l + n);
    _gl_rev(v, l + n, r);
    _gl_rev(v, l, r);
}

static inline int getopt_long(int argc, char *const argv_const[],
                              const char *optstring,
                              const struct option *longopts, int *longindex) {
    (void)optstring;
    char **argv = (char **)argv_const; /* permutation requires reordering */

    /*
     * Scan forward from optind, skipping non-options.  Count them so we
     * can rotate them to the end once we find an option (or end-of-args).
     */
    int base  = optind;   /* where we started scanning this call */
    int nopts = 0;        /* number of non-option args skipped   */

    while (optind < argc) {
        char *a = argv[optind];
        /* Non-option: bare word or single "-" */
        if (a[0] != '-' || a[1] == '\0') { ++nopts; ++optind; continue; }
        /* "--" sentinel: rotate non-opts to end, then stop */
        if (a[1] == '-' && a[2] == '\0') {
            if (nopts) _gl_rotate(argv, base, argc, nopts);
            /* advance past "--" itself; non-opts are now at the tail */
            optind = base + 1;
            return -1;
        }
        /* Found a "--name" option. */
        break;
    }

    if (optind >= argc) {
        /* End of argv: non-opts already sit at base..argc-1 after scanning. */
        optind = base;
        return -1;
    }

    /*
     * We have an option at argv[optind] with nopts non-option args before
     * it (indices base..optind-1).  Determine how many argv slots the
     * option itself consumes (1 for --flag/--opt=val, 2 for --opt val)
     * so we rotate exactly those slots forward, keeping the value token
     * adjacent to its option before the non-opts land after them.
     */
    char *arg        = argv[optind];
    const char *name = arg + 2;
    const char *eq   = strchr(name, '=');
    size_t nlen      = eq ? (size_t)(eq - name) : strlen(name);

    /* Find the matching option entry to know has_arg before rotating. */
    int matched = -1;
    for (int i = 0; longopts[i].name; ++i) {
        if (strncmp(longopts[i].name, name, nlen) == 0 &&
            longopts[i].name[nlen] == '\0') { matched = i; break; }
    }

    /* How many slots does this option consume? */
    int width = 1;
    if (!eq && matched >= 0 && longopts[matched].has_arg == required_argument
            && optind + 1 < argc)
        width = 2;

    /* Rotate: move nopts non-option tokens past the option (+ value). */
    if (nopts) _gl_rotate(argv, base, base + nopts + width, nopts);
    optind = base; /* option token is now at base */

    if (matched < 0) {
        if (opterr) fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], argv[optind]);
        optopt = 0;
        ++optind;
        return '?';
    }

    if (longindex) *longindex = matched;
    ++optind; /* consume option token */

    if (longopts[matched].has_arg == required_argument) {
        if (eq) {
            optarg = (char *)(eq + 1);
        } else if (optind < argc) {
            optarg = argv[optind++]; /* consume value token */
        } else {
            if (opterr) fprintf(stderr, "%s: option '--%s' requires an argument\n",
                                argv[0], longopts[matched].name);
            optarg = NULL;
            return '?';
        }
    } else if (longopts[matched].has_arg == optional_argument) {
        optarg = eq ? (char *)(eq + 1) : NULL;
    } else {
        if (eq) {
            if (opterr) fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n",
                                argv[0], longopts[matched].name);
            return '?';
        }
        optarg = NULL;
    }

    if (longopts[matched].flag) { *longopts[matched].flag = longopts[matched].val; return 0; }
    return longopts[matched].val;
}

#endif /* _BS_GETOPT_H_ */
