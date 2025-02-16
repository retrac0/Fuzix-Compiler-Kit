#include "libfp.h"

/* A2 <= A1 */
unsigned _cclteqf(uint32_t a1, uint32_t a2)
{
    /* Handle negative zero compare */
    if ((a1 & a2 & 0x7FFFFFFFUL) == 0)
        return 1;
    /* Both negative */
    if ((a1 & a2) & 0x80000000UL) {
        /* Mantissa is not negated */
        if ((int32_t)a1 > (int32_t) a2)
            return 0;
        return 1;
    }
    /* Can rely on sign to sort compare correctly */
    if ((int32_t)a2 > (int32_t)a1)
        return 0;
    return 1;
}
