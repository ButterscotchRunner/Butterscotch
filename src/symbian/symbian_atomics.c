// shadows unimplemented compiler builtins for miniaudio to work

#include <e32atomics.h>

TUint32 __sync_fetch_and_add_4(volatile void* a, TUint32 v) {
    return __e32_atomic_add_ord32((volatile TAny*)a, v);
}

TUint32 __sync_fetch_and_sub_4(volatile void* a, TUint32 v) {
    return __e32_atomic_add_ord32((volatile TAny*)a, (TUint32)(0u - v));
}

TUint32 __sync_fetch_and_and_4(volatile void* a, TUint32 v) {
    return __e32_atomic_and_ord32((volatile TAny*)a, v);
}

TUint32 __sync_fetch_and_or_4(volatile void* a, TUint32 v) {
    return __e32_atomic_ior_ord32((volatile TAny*)a, v);
}

void __sync_lock_release_4(volatile void* a) {
    __e32_atomic_store_rel32((volatile TAny*)a, 0);
}

TUint32 __sync_lock_test_and_set_4(volatile void* a, TUint32 v) {
    return __e32_atomic_swp_acq32((volatile TAny*)a, v);
}

void __sync_synchronize(void) {
    __e32_memory_barrier();
}

TUint32 __sync_val_compare_and_swap_4(volatile void* a, TUint32 aQ, TUint32 v) {
    TUint32 q = aQ;
    __e32_atomic_cas_ord32((volatile TAny*)a, &q, v);
    return q;
}
