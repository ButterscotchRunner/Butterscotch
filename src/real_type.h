#ifndef _BS_REAL_TYPE_H_
#define _BS_REAL_TYPE_H_

#include "common.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include "log.h"

#ifndef INFINITY
#define INFINITY ((float)1e39)
#endif

#undef isnan
#undef isinf
#define isnan(x) (x != x)
#define isinf(x) ((x) == INFINITY || (x) == -INFINITY)

#ifdef USE_FLOAT_REALS

typedef float GMLReal;

#define GMLReal_sin sinf
#define GMLReal_cos cosf
#define GMLReal_tan tanf
#define GMLReal_acos acosf
#define GMLReal_asin asinf
#define GMLReal_atan atanf
#define GMLReal_atan2 atan2f
#define GMLReal_sqrt sqrtf
#define GMLReal_fabs fabsf
#define GMLReal_fmod fmodf
#define GMLReal_floor floorf
#define GMLReal_ceil ceilf
#define GMLReal_round roundf
#define GMLReal_pow powf
#define GMLReal_log logf
#define GMLReal_log2 log2f
#define GMLReal_log10 log10f
#define GMLReal_fmax fmaxf
#define GMLReal_fmin fminf
#define GMLReal_nextafter nextafterf
#define GMLReal_strtod(str, endptr) strtof(str, endptr)

#else

#ifdef USE_FIXED_REALS

#ifndef __cplusplus
#error USE_FIXED_REALS requires compiling as C++
#endif

class GMLReal {
public:
    GMLReal() = default;
    GMLReal(signed char i)        : raw_((int64_t)i << FRAC_BITS) {}
    GMLReal(unsigned char i)      : raw_((int64_t)i << FRAC_BITS) {}
    GMLReal(signed short i)       : raw_((int64_t)i << FRAC_BITS) {}
    GMLReal(unsigned short i)     : raw_((int64_t)i << FRAC_BITS) {}
    GMLReal(signed int i)         : raw_((int64_t)i << FRAC_BITS) {}
    GMLReal(unsigned int i)       : raw_((int64_t)i << FRAC_BITS) {}
    GMLReal(signed long i)        : raw_((int64_t)i << FRAC_BITS) {}
    GMLReal(unsigned long i)      : raw_((int64_t)i << FRAC_BITS) {}
#ifdef _MSC_VER
    GMLReal(__int64 i)            : raw_((int64_t)i << FRAC_BITS) {}
    GMLReal(__uint64 i)           : raw_((int64_t)i << FRAC_BITS) {}
#else
    GMLReal(long long i)          : raw_((int64_t)i << FRAC_BITS) {}
    GMLReal(unsigned long long i) : raw_((int64_t)i << FRAC_BITS) {}
#endif

    GMLReal(double d) {
        if (isnan(d)) {
            logWarn("GMLReal: NaN cast to fixed-point value, treating as 0\n");
            raw_ = 0;
        }
        else if (d >=  INFINITY) raw_ =  INT64_MAX;
        else if (d <= -INFINITY) raw_ = -INT64_MAX;
        else raw_ = (int64_t)(d * (double)(INT64_C(1) << FRAC_BITS));
    }

    GMLReal(float f) {
        if (isnanf(f)) {
            logWarn("GMLReal: NaN cast to fixed-point value, treating as 0\n");
            raw_ = 0;
        }
        else if (f >=  INFINITY) raw_ =  INT64_MAX;
        else if (f <= -INFINITY) raw_ = -INT64_MAX;
        else raw_ = (int64_t)(f * (float)(INT64_C(1) << FRAC_BITS));
    }

    static GMLReal from_raw(int64_t r) {
        GMLReal f;
        f.raw_ = r;
        return f;
    }

    static GMLReal infinity()     { return from_raw(INT64_MAX); }
    static GMLReal neg_infinity() { return from_raw(INT64_MIN); }

    bool is_pos_infinite() const { return raw_ == INT64_MAX; }
    bool is_neg_infinite() const { return raw_ == INT64_MIN; }
    bool is_infinite()     const { return is_pos_infinite() || is_neg_infinite(); }

    operator signed char()        const { return raw_ >> FRAC_BITS; }
    operator unsigned char()      const { return raw_ >> FRAC_BITS; }
    operator signed short()       const { return raw_ >> FRAC_BITS; }
    operator unsigned short()     const { return raw_ >> FRAC_BITS; }
    operator signed int()         const { return raw_ >> FRAC_BITS; }
    operator unsigned int()       const { return raw_ >> FRAC_BITS; }
    operator signed long()        const { return raw_ >> FRAC_BITS; }
    operator unsigned long()      const { return raw_ >> FRAC_BITS; }
#ifdef _MSC_VER
    operator __int64()            const { return raw_ >> FRAC_BITS; }
    operator __uint64()           const { return raw_ >> FRAC_BITS; }
#else
    operator signed long long()   const { return raw_ >> FRAC_BITS; }
    operator unsigned long long() const { return raw_ >> FRAC_BITS; }
#endif

    operator double() const {
        if (is_pos_infinite()) return INFINITY;
        if (is_neg_infinite()) return -INFINITY;
        return (double)raw_ / (double)(INT64_C(1) << FRAC_BITS);
    }

    operator float() const {
        if (is_pos_infinite()) return INFINITY;
        if (is_neg_infinite()) return -INFINITY;
        return (float)raw_ / (float)(INT64_C(1) << FRAC_BITS);
    }

    GMLReal operator-() const { return from_raw(-raw_); } // safe: never holds INT64_MIN

    GMLReal operator+(const GMLReal& o) const {
        if (is_infinite() || o.is_infinite()) {
            if (is_infinite() && o.is_infinite() && raw_ != o.raw_)
                return from_raw(0); // inf + -inf: indeterminate, no NaN available
            return is_infinite() ? *this : o;
        }
        return from_raw(raw_ + o.raw_);
    }

    GMLReal operator-(const GMLReal& o) const { return *this + (-o); }

    // NOTE: naive 64-bit multiply/divide for the finite case. The
    // intermediate product can overflow 64 bits once both operands'
    // magnitudes get large (roughly when integer parts exceed ~2^24
    // each); with the sentinels in place that overflow will now often
    // land on or near +/-INT64_MAX and read as infinity rather than
    // silently wrapping. Fine for typical game-world coordinates; flag
    // if you need a proper wide-multiply instead.
    GMLReal operator*(const GMLReal& o) const {
        if (is_infinite() || o.is_infinite()) {
            bool neg = (raw_ < 0) != (o.raw_ < 0);
            return neg ? neg_infinity() : infinity();
        }
        return from_raw((raw_ * o.raw_) >> FRAC_BITS);
    }

    GMLReal operator/(const GMLReal& o) const {
        if (is_infinite() && o.is_infinite())
            return from_raw(0); // inf / inf: indeterminate
        if (o.is_infinite())
            return from_raw(0); // finite / inf = 0
        if (is_infinite()) {
            bool neg = (raw_ < 0) != (o.raw_ < 0);
            return neg ? neg_infinity() : infinity();
        }
        if (o.raw_ == 0)
            return raw_ < 0 ? neg_infinity() : infinity(); // div by zero -> inf
        return from_raw((raw_ << FRAC_BITS) / o.raw_);
    }

    GMLReal& operator+=(const GMLReal& o) { *this = *this + o; return *this; }
    GMLReal& operator-=(const GMLReal& o) { *this = *this - o; return *this; }
    GMLReal& operator*=(const GMLReal& o) { *this = *this * o; return *this; }
    GMLReal& operator/=(const GMLReal& o) { *this = *this / o; return *this; }

    GMLReal& operator++()    { *this += GMLReal(1); return *this; }
    GMLReal  operator++(int) { GMLReal tmp = *this; *this += GMLReal(1); return tmp; }
    GMLReal& operator--()    { *this -= GMLReal(1); return *this; }
    GMLReal  operator--(int) { GMLReal tmp = *this; *this -= GMLReal(1); return tmp; }

    bool operator==(const GMLReal& o) const { return raw_ == o.raw_; }
    bool operator!=(const GMLReal& o) const { return raw_ != o.raw_; }
    bool operator<(const GMLReal& o)  const { return raw_ <  o.raw_; }
    bool operator<=(const GMLReal& o) const { return raw_ <= o.raw_; }
    bool operator>(const GMLReal& o)  const { return raw_ >  o.raw_; }
    bool operator>=(const GMLReal& o) const { return raw_ >= o.raw_; }

    GMLReal operator+(double d) const { return *this + GMLReal(d); }
    GMLReal operator-(double d) const { return *this - GMLReal(d); }
    GMLReal operator*(double d) const { return *this * GMLReal(d); }
    GMLReal operator/(double d) const { return *this / GMLReal(d); }

    friend GMLReal operator+(double d, const GMLReal& r) { return GMLReal(d) + r; }
    friend GMLReal operator-(double d, const GMLReal& r) { return GMLReal(d) - r; }
    friend GMLReal operator*(double d, const GMLReal& r) { return GMLReal(d) * r; }
    friend GMLReal operator/(double d, const GMLReal& r) { return GMLReal(d) / r; }

    // Exact-match overloads against double, for both arithmetic and
    // comparisons. Without these, something like `f > 0.5` or `f + 1.0`
    // is ambiguous: the compiler can equally convert f to double (via
    // operator double()) or the literal to GMLReal (via the double
    // constructor), and both conversions rank the same. Giving it a
    // candidate that needs no conversion at all removes the tie.
    bool operator==(double d) const { return raw_ == GMLReal(d).raw_; }
    bool operator!=(double d) const { return raw_ != GMLReal(d).raw_; }
    bool operator<(double d)  const { return raw_ <  GMLReal(d).raw_; }
    bool operator<=(double d) const { return raw_ <= GMLReal(d).raw_; }
    bool operator>(double d)  const { return raw_ >  GMLReal(d).raw_; }
    bool operator>=(double d) const { return raw_ >= GMLReal(d).raw_; }

    friend bool operator==(double d, const GMLReal& r) { return r == d; }
    friend bool operator!=(double d, const GMLReal& r) { return r != d; }
    friend bool operator<(double d,  const GMLReal& r) { return r >  d; }
    friend bool operator<=(double d, const GMLReal& r) { return r >= d; }
    friend bool operator>(double d,  const GMLReal& r) { return r <  d; }
    friend bool operator>=(double d, const GMLReal& r) { return r <= d; }

    static const int FRAC_BITS = 16;

private:
    int64_t raw_;
};

#else

typedef double GMLReal;

#endif

#define GMLReal_sin(x) sin((double)(x))
#define GMLReal_cos(x) cos((double)(x))
#define GMLReal_tan(x) tan((double)(x))
#define GMLReal_acos(x) acos((double)(x))
#define GMLReal_asin(x) asin((double)(x))
#define GMLReal_atan(x) atan((double)(x))
#define GMLReal_atan2(x,y) atan2((double)(x),(double)(y))
#define GMLReal_sqrt(x) sqrt((double)(x))
#define GMLReal_fabs(x) fabs((double)(x))
#define GMLReal_fmod(x,y) fmod((double)(x),(double)(y))
#define GMLReal_floor(x) floor((double)(x))
#define GMLReal_ceil(x) ceil((double)(x))
#define GMLReal_round(x) round((double)(x))
#define GMLReal_pow(x,y) pow((double)(x),(double)(y))
#define GMLReal_log(x) log((double)(x))
#define GMLReal_log2(x) log2((double)(x))
#define GMLReal_log10(x) log10((double)(x))
#define GMLReal_fmax(x,y) fmax((double)(x),(double)(y))
#define GMLReal_fmin(x,y) fmin((double)(x),(double)(y))
#define GMLReal_nextafter nextafter
#define GMLReal_strtod(str, endptr) strtod(str, endptr)

#endif

// Round-half-to-even (banker's rounding).
// While the original runner uses "llrint(double)", we use our own banker's rounding implementation to avoid quirks in specific platforms (like the PlayStation 2) having different llrint rounding implementations.
static inline GMLReal GMLReal_bankersRound(GMLReal v) {
    if (isnan(v) || isinf(v)) return v;
    GMLReal f = GMLReal_floor(v);
    GMLReal frac = v - f;
    if (0.5 > frac) return f;
    if (frac > 0.5) return f + 1.0;
    // Exactly halfway: round to the even neighbor.
    int64_t fi = (int64_t) f;
    return (fi & 1) == 0 ? f : f + 1.0;
}

#endif /* _BS_REAL_TYPE_H_ */
