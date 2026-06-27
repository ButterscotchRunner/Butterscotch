#include <stdlib.h>

void bz_internal_error(int errcode)
{
    (void)errcode;
    abort();
}
