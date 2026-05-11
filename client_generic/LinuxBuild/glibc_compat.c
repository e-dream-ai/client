/*
 * glibc_compat.c — Caps glibc symbol version requirements to <= GLIBC_2.35.
 *
 * Problem: building on Arch Linux (glibc 2.41+) with g++ produces an ELF that
 * references versioned symbols like sqrtf@GLIBC_2.43 and __isoc23_strtol
 * (GLIBC_2.38). These don't exist on Ubuntu 22.04 (glibc 2.35), causing:
 *   ./infinidream: /lib/x86_64-linux-gnu/libm.so.6: version 'GLIBC_2.43' not found
 *
 * Solution: define each problematic symbol here (satisfying ALL references at
 * link time — including those from pre-built archives like libsioclient_tls.a),
 * and inside each wrapper call a version-pinned alias that binds to a glibc
 * version present since at least Ubuntu 22.04.
 *
 * How it works:
 *   __asm__(".symver __real_foo,foo@GLIBC_2.2.5") on an extern declaration
 *   causes the linker to resolve __real_foo via the versioned symbol
 *   foo@GLIBC_2.2.5 in the dynamic library (libm/libc). Our definition of
 *   foo() then satisfies every unversioned reference to foo from static
 *   archives, which is what the linker would otherwise bind to the current
 *   host's default (e.g. GLIBC_2.43).
 *
 * Compile flags required:
 *   -std=c11         (disables C23 header redirections like strtol→__isoc23_strtol)
 *   -fno-builtin     (prevents GCC from replacing calls with inline SSE/AVX)
 *   -O1              (minimal optimisation — just enough to not bloat, not enough
 *                     to re-introduce inlining of the wrapped calls)
 */

/* -std=c11 ensures __STDC_VERSION__ = 201112L, so __GLIBC_USE(ISOC_2X) = 0
 * and <stdio.h> / <stdlib.h> do NOT remap fscanf/strtol to __isoc23_* variants. */
#include <stdio.h>   /* FILE, va_list scaffold for scanf wrappers */
#include <stdarg.h>

/* ==========================================================================
 * Math wrappers: GLIBC_2.43 → GLIBC_2.2.5
 *
 * On glibc 2.43, sqrtf/acosf/atan2f/fmod/fmodf default to a new symbol
 * version.  We interpose them here so the binary's dynamic table only
 * references GLIBC_2.2.5 (present in every glibc since glibc 2.0).
 * ========================================================================== */

/* sqrtf */
extern float __real_sqrtf(float);
__asm__(".symver __real_sqrtf,sqrtf@GLIBC_2.2.5");
__attribute__((visibility("hidden"))) float sqrtf(float x) { return __real_sqrtf(x); }

/* acosf */
extern float __real_acosf(float);
__asm__(".symver __real_acosf,acosf@GLIBC_2.2.5");
__attribute__((visibility("hidden"))) float acosf(float x) { return __real_acosf(x); }

/* atan2f */
extern float __real_atan2f(float, float);
__asm__(".symver __real_atan2f,atan2f@GLIBC_2.2.5");
__attribute__((visibility("hidden"))) float atan2f(float y, float x) { return __real_atan2f(y, x); }

/* fmod */
extern double __real_fmod(double, double);
__asm__(".symver __real_fmod,fmod@GLIBC_2.2.5");
__attribute__((visibility("hidden"))) double fmod(double x, double y) { return __real_fmod(x, y); }

/* fmodf */
extern float __real_fmodf(float, float);
__asm__(".symver __real_fmodf,fmodf@GLIBC_2.2.5");
__attribute__((visibility("hidden"))) float fmodf(float x, float y) { return __real_fmodf(x, y); }

/* ==========================================================================
 * C23 strtol / strtoull family: GLIBC_2.38 → GLIBC_2.2.5
 *
 * On glibc 2.38+, when code is compiled in C23 mode (or by a compiler that
 * uses _ISOC23_SOURCE), strtol/strtoll/strtoul/strtoull are internally
 * renamed to __isoc23_strtol etc.  Pre-built archives compiled on Arch may
 * contain references to these names.  We provide the definitions here so
 * they resolve without touching glibc 2.38 symbols.
 * ========================================================================== */

extern long __real_strtol(const char *, char **, int);
__asm__(".symver __real_strtol,strtol@GLIBC_2.2.5");
long __isoc23_strtol(const char *nptr, char **endptr, int base) {
    return __real_strtol(nptr, endptr, base);
}

extern long long __real_strtoll(const char *, char **, int);
__asm__(".symver __real_strtoll,strtoll@GLIBC_2.2.5");
long long __isoc23_strtoll(const char *nptr, char **endptr, int base) {
    return __real_strtoll(nptr, endptr, base);
}

extern unsigned long __real_strtoul(const char *, char **, int);
__asm__(".symver __real_strtoul,strtoul@GLIBC_2.2.5");
unsigned long __isoc23_strtoul(const char *nptr, char **endptr, int base) {
    return __real_strtoul(nptr, endptr, base);
}

extern unsigned long long __real_strtoull(const char *, char **, int);
__asm__(".symver __real_strtoull,strtoull@GLIBC_2.2.5");
unsigned long long __isoc23_strtoull(const char *nptr, char **endptr, int base) {
    return __real_strtoull(nptr, endptr, base);
}

/* ==========================================================================
 * C23 scanf family: GLIBC_2.38 → GLIBC_2.2.5
 *
 * Same issue as strtol: __isoc23_fscanf / __isoc23_sscanf are the C23
 * variants that pre-built code on modern Arch may reference.  We delegate
 * to the variadic vfscanf/vsscanf@GLIBC_2.2.5 equivalents.
 * ========================================================================== */

extern int __real_vfscanf(FILE *, const char *, va_list);
__asm__(".symver __real_vfscanf,vfscanf@GLIBC_2.2.5");
int __isoc23_fscanf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = __real_vfscanf(stream, fmt, ap);
    va_end(ap);
    return r;
}

extern int __real_vsscanf(const char *, const char *, va_list);
__asm__(".symver __real_vsscanf,vsscanf@GLIBC_2.2.5");
int __isoc23_sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = __real_vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}
