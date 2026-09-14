#ifndef DOOM3DO_INTMATH_H
#define DOOM3DO_INTMATH_H
#include "Burger.h"
static inline Fixed IMFixMul(Fixed a, Fixed b) {
    return (Fixed)(((int64_t)a * (int64_t)b) >> FRACBITS);
}
static inline Fixed IMFixDiv(Fixed a, Fixed b) {
    if (b == 0)
        return (a < 0) ? (Fixed)INT32_MIN : (Fixed)INT32_MAX;
    return (Fixed)(((int64_t)a << FRACBITS) / b);
}
#endif
