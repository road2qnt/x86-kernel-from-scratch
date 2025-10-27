#ifndef UTILS_H
#define UTILS_H

#include "stdtype.h" // Butuh uint8_t, uint32_t

/**
 * @brief Set bit ke-n (mulai dari 0) menjadi 1 di dalam buffer byte.
 * @param buf Pointer ke buffer byte (misal: bitmap).
 * @param bit Indeks bit yang mau di-set (0, 1, 2, ...).
 */
static inline void set_bit(uint8_t *buf, uint32_t bit) {
    // Cari byte ke berapa: bit / 8
    // Cari bit ke berapa di dalam byte itu: bit % 8
    // Gunakan OR (|) dengan 1 yang sudah digeser ke posisi bit itu
    buf[bit / 8] |= (1 << (bit % 8));
}

/**
 * @brief Clear bit ke-n (mulai dari 0) menjadi 0 di dalam buffer byte.
 * @param buf Pointer ke buffer byte.
 * @param bit Indeks bit yang mau di-clear.
 */
static inline void clear_bit(uint8_t *buf, uint32_t bit) {
    // Sama seperti set_bit, tapi pake AND (&) dengan NOT (~)
    // ~(1 << (bit % 8)) akan membuat mask dengan semua bit 1 kecuali di posisi target
    buf[bit / 8] &= ~(1 << (bit % 8));
}

/**
 * @brief Tes nilai bit ke-n (mulai dari 0) di dalam buffer byte.
 * @param buf Pointer ke buffer byte.
 * @param bit Indeks bit yang mau dites.
 * @return Nilai non-zero jika bitnya 1, return 0 jika bitnya 0.
 */
static inline uint8_t test_bit(uint8_t *buf, uint32_t bit) {
    // Gunakan AND (&) untuk mengecek apakah bit di posisi itu 1
    return buf[bit / 8] & (1 << (bit % 8));
}

#endif // UTILS_H