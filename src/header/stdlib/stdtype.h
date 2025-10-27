#ifndef STDTYPE_H
#define STDTYPE_H

// Unsigned integer types (hanya positif)
typedef unsigned char       uint8_t;    // Dijamin pas 1 byte (8 bit)
typedef unsigned short      uint16_t;   // Dijamin pas 2 byte (16 bit)
typedef unsigned int        uint32_t;   // Dijamin pas 4 byte (32 bit)
// typedef unsigned long long  uint64_t;   // Opsional jika butuh 64-bit

// Signed integer types (bisa negatif)
typedef signed char         int8_t;     // Dijamin pas 1 byte
typedef signed short        int16_t;    // Dijamin pas 2 byte
typedef signed int          int32_t;    // Dijamin pas 4 byte
// typedef signed long long    int64_t;    // Opsional jika butuh 64-bit

// Ukuran pointer atau size_t (berguna nanti)
typedef uint32_t            size_t;

#endif // STDTYPE_H