#pragma once

#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DBG_LOG(fmt, ...) do { fprintf(stderr, fmt, ##__VA_ARGS__); fprintf(stderr, " at %s in %s:%d\n", __PRETTY_FUNCTION__, __FILE__, __LINE__); } while(0)
#define FATAL(status, fmt, ...) do { DBG_LOG(fmt, ##__VA_ARGS__); exit(status); } while(0)

#define CHECK_PTR_TYPE(x, type) ((x)-(type)(x) + (type)(x))		/** Check that a pointer @x is of type @type. Fail compilation if not. **/
#define PTR_TO(s, i) &((s*)0)->i					/** Return OFFSETOF() in form of a pointer. **/
#define OFFSETOF(s, i) ((uint)offsetof(s, i))				/** Offset of item @i from the start of structure @s **/
#define SKIP_BACK(s, i, p) ((s *)((char *)p - OFFSETOF(s, i)))		/** Given a pointer @p to item @i of structure @s, return a pointer to the start of the struct. **/

/** Align an integer @s to the nearest higher multiple of @a (which should be a power of two) **/
#define ALIGN_TO(s, a) (((s)+a-1)&~(a-1))

/** Align a pointer @p to the nearest higher multiple of @s. **/
#define ALIGN_PTR(p, s) ((uintptr_t)(p) % (s) ? (typeof(p))((uintptr_t)(p) + (s) - (uintptr_t)(p) % (s)) : (p))

#define UNALIGNED_PART(ptr, type) (((uintptr_t) (ptr)) % sizeof(type))

/*** === Other utility macros ***/

#define MIN(a,b) (((a)<(b))?(a):(b))			/** Minimum of two numbers **/
#define MAX(a,b) (((a)>(b))?(a):(b))			/** Maximum of two numbers **/
#define CLAMP(x,min,max) ({ typeof(x) _t=x; (_t < min) ? min : (_t > max) ? max : _t; })	/** Clip a number @x to interval [@min,@max] **/
#define ABS(x) ((x) < 0 ? -(x) : (x))			/** Absolute value **/
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))		/** The number of elements of an array **/
#define STRINGIFY(x) #x					/** Convert macro parameter to a string **/
#define STRINGIFY_EXPANDED(x) STRINGIFY(x)		/** Convert an expanded macro parameter to a string **/
#define GLUE(x,y) x##y					/** Glue two tokens together **/
#define GLUE_(x,y) x##_##y				/** Glue two tokens together, separating them by an underscore **/

#define COMPARE(x,y) do { if ((x)<(y)) return -1; if ((x)>(y)) return 1; } while(0)		/** Numeric comparison function for qsort() **/
#define REV_COMPARE(x,y) COMPARE(y,x)								/** Reverse numeric comparison **/
#define COMPARE_LT(x,y) do { if ((x)<(y)) return 1; if ((x)>(y)) return 0; } while(0)
#define COMPARE_GT(x,y) COMPARE_LT(y,x)

#define	ROL(x, bits) (((x) << (bits)) | ((uint)(x) >> (sizeof(uint)*8 - (bits))))		/** Bitwise rotation of an unsigned int to the left **/
#define	ROR(x, bits) (((uint)(x) >> (bits)) | ((x) << (sizeof(uint)*8 - (bits))))		/** Bitwise rotation of an unsigned int to the right **/

/*** === Shortcuts for GCC Extensions ***/

#define NONRET __attribute__((noreturn))				/** Function does not return **/
#define UNUSED __attribute__((unused))					/** Variable/parameter is knowingly unused **/
#define CONSTRUCTOR __attribute__((constructor))			/** Call function upon start of program **/
#define CONSTRUCTOR_WITH_PRIORITY(p) __attribute__((constructor(p)))	/** Define constructor with a given priority **/
#define PACKED __attribute__((packed))					/** Structure should be packed **/
#define CONST __attribute__((const))					/** Function depends only on arguments **/
#define PURE __attribute__((pure))					/** Function depends only on arguments and global vars **/
#define FORMAT_CHECK(x,y,z) __attribute__((format(x,y,z)))		/** Checking of printf-like format strings **/
#define likely(x) __builtin_expect((x),1)				/** Use `if (likely(@x))` if @x is almost always true **/
#define unlikely(x) __builtin_expect((x),0)				/** Use `if (unlikely(@x))` to hint that @x is almost always false **/

#define ALWAYS_INLINE inline __attribute__((always_inline))		/** Forcibly inline **/
#define NO_INLINE __attribute__((noinline))				/** Forcibly uninline **/
#define LIKE_MALLOC __attribute__((malloc))				/** Function returns a "new" pointer **/
#define SENTINEL_CHECK __attribute__((sentinel))			/** The last argument must be NULL **/
#define ARRAY_LEN(ARR) (sizeof(ARR)/sizeof(*(ARR)))

#define XMALLOC(size) ({                                           \
    void *$p = malloc((size));                                     \
    if (!$p)                                                       \
        FATAL(255, "Cannot allocate %zu bytes of memory", (size)); \
                                                                   \
    $p;                                                            \
})

#define XMALLOC_ZERO(size) ({    \
    void *$p = XMALLOC((size));  \
    bzero($p, size);             \
                                 \
    $p;                          \
})

#define XREALLOC(ptr, size) ({                                      \
    void *$p = realloc(ptr, (size));                                \
    if ((size) > 0 && !$p)                                          \
        FATAL(255, "Cannot reallocate %zu bytes of memory", (size));\
                                                                    \
    $p;                                                             \
})

#define XFREE(ptr) free(ptr)
