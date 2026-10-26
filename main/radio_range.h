// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define LR_MIN_HZ 100000000u
#define LR_MAX_HZ 2700000000u
#define LR_HF_HZ 1500000000u
#ifdef __cplusplus
#define LR_CONSTEXPR constexpr
#else
#define LR_CONSTEXPR
#endif
static inline LR_CONSTEXPR bool lr_frequency_valid(uint32_t hz) {
    return hz >= LR_MIN_HZ && hz <= LR_MAX_HZ;
}
static inline LR_CONSTEXPR bool lr_calibration_acceptable(uint32_t hz, uint16_t errors) {
    // Upstream measured usable RX with IMG_CALIB_ERR (bit 7) at 100/103 MHz.
    // Only tolerate this specific warning below 106 MHz, never PLL or other errors.
    uint16_t allowed = hz >= LR_MIN_HZ && hz < 106000000u ? 0x0080u : 0;
    return lr_frequency_valid(hz) && !(errors & (uint16_t)~allowed);
}
#undef LR_CONSTEXPR
