#ifndef _STRING_H
#define _STRING_H

#include "stdtype.h"

#ifdef FS_INSERTER
    #include <string.h> 
#else
    void* memcpy(void* dest, const void* src, size_t n);
    void* memset(void *s, int c, size_t n);
    int memcmp(const void *s1, const void *s2, size_t n);
    void* memmove(void* dest, const void* src, size_t n);
    /**
     * memset16 - Set 16-bit values in memory
     * @ptr: Pointer to the block of memory to fill
     * @value: Value to be set (16-bit)
     * @num: Number of 16-bit elements to be set
     */
    void* memset16(void *ptr, int value, size_t num);
#endif

#endif