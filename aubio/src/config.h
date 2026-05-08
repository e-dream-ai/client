#ifndef CMAKE_CONFIG_H
#define CMAKE_CONFIG_H

#define HAVE_C99_VARARGS_MACROS 1

#define HAVE_STDLIB_H 1
#define HAVE_STDIO_H 1
#define HAVE_MATH_H 1
#define HAVE_STRING_H 1
#define HAVE_ERRNO_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDARG_H 1

/* no FFTW3, no Accelerate, no Intel IPP — uses Ooura FFT */
/* no SNDFILE, no SAMPLERATE, no libav — we supply our own samples */

/* MSVC compatibility */
#ifdef _MSC_VER
#define strncasecmp _strnicmp
#endif

#endif /* CMAKE_CONFIG_H */
