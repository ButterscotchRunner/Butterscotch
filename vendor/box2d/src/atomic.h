// SPDX-FileCopyrightText: 2023 Erin Catto
// SPDX-License-Identifier: MIT

#pragma once

#include "core.h"

#include <stdint.h>

#if defined( _MSC_VER ) && !defined(B2_SINGLE_THREADED)
#include <intrin.h>
#endif

static inline void b2AtomicStoreInt( b2AtomicInt* a, int value )
{
#if defined(B2_SINGLE_THREADED)
	a->value = value;
#elif defined( _MSC_VER )
	(void)_InterlockedExchange( (long*)&a->value, value );
#elif defined( __GNUC__ ) || defined( __clang__ )
	__atomic_store_n( &a->value, value, __ATOMIC_SEQ_CST );
#else
#error "Unsupported platform"
#endif
}

static inline int b2AtomicLoadInt( b2AtomicInt* a )
{
#if defined(B2_SINGLE_THREADED)
	return a->value;
#elif defined( _MSC_VER )
	return _InterlockedOr( (long*)&a->value, 0 );
#elif defined( __GNUC__ ) || defined( __clang__ )
	return __atomic_load_n( &a->value, __ATOMIC_SEQ_CST );
#else
#error "Unsupported platform"
#endif
}

static inline int b2AtomicFetchAddInt( b2AtomicInt* a, int increment )
{
#if defined(B2_SINGLE_THREADED)
	int previous = a->value;
	a->value += increment;
	return previous;
#elif defined( _MSC_VER )
	return _InterlockedExchangeAdd( (long*)&a->value, (long)increment );
#elif defined( __GNUC__ ) || defined( __clang__ )
	return __atomic_fetch_add( &a->value, increment, __ATOMIC_SEQ_CST );
#else
#error "Unsupported platform"
#endif
}

static inline bool b2AtomicCompareExchangeInt( b2AtomicInt* a, int expected, int desired )
{
#if defined(B2_SINGLE_THREADED)
	if (a->value != expected) return false;
	a->value = desired;
	return true;
#elif defined( _MSC_VER )
	return _InterlockedCompareExchange( (long*)&a->value, (long)desired, (long)expected ) == expected;
#elif defined( __GNUC__ ) || defined( __clang__ )
	// The value written to expected is ignored
	return __atomic_compare_exchange_n( &a->value, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST );
#else
#error "Unsupported platform"
#endif
}

static inline void b2AtomicStoreU32( b2AtomicU32* a, uint32_t value )
{
#if defined(B2_SINGLE_THREADED)
	a->value = value;
#elif defined( _MSC_VER )
	(void)_InterlockedExchange( (long*)&a->value, value );
#elif defined( __GNUC__ ) || defined( __clang__ )
	__atomic_store_n( &a->value, value, __ATOMIC_SEQ_CST );
#else
#error "Unsupported platform"
#endif
}

static inline uint32_t b2AtomicLoadU32( b2AtomicU32* a )
{
#if defined(B2_SINGLE_THREADED)
	return a->value;
#elif defined( _MSC_VER )
	return (uint32_t)_InterlockedOr( (long*)&a->value, 0 );
#elif defined( __GNUC__ ) || defined( __clang__ )
	return __atomic_load_n( &a->value, __ATOMIC_SEQ_CST );
#else
#error "Unsupported platform"
#endif
}
