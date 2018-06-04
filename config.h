#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdint.h>

#define true  1
#define false 0
#define STATIC  static
#define INLINE  inline

#ifdef _MSC_BUILD
#define fseeko64	_fseeki64
#define ftello64	_ftelli64
typedef __int64     off64_t;
typedef unsigned char                   uint8_t;
typedef unsigned short int              uint16_t;
typedef unsigned int                    uint32_t;
typedef unsigned long long              uint64_t;
typedef float                           float32;
typedef double                          float64;
typedef char                            bool_t;
#endif

#endif
