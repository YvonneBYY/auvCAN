/*
 * auvcan_bittiming.h - pure bit-timing calculator (no HAL), unit-tested on the host.
 *
 * Picks the smallest prescaler that divides the kernel clock exactly and gives
 * 8..80 time quanta per bit, then places the sample point at the nearest
 * quantum to `sample_permille` (875 = 87.5 %, the DroneCAN recommendation).
 *
 *   16 MHz  -> presc 1, seg1 13, seg2  2  (16 tq, 87.5 %)
 *   24 MHz  -> presc 1, seg1 20, seg2  3  (24 tq, 87.5 %)
 *   80 MHz  -> presc 1, seg1 69, seg2 10  (80 tq, 87.5 %)
 *   170 MHz -> presc 5, seg1 29, seg2  4  (34 tq, 88.2 %)
 */
#ifndef AUVCAN_BITTIMING_H
#define AUVCAN_BITTIMING_H

#include <stdint.h>

typedef struct {
    uint32_t prescaler;
    uint32_t seg1;      /* prop + phase1, in tq (excludes the sync quantum) */
    uint32_t seg2;      /* phase2, in tq */
    uint32_t sjw;
} auvcan_bit_timing_t;

static inline int auvcan_compute_bit_timing(uint32_t clk_hz, uint32_t bitrate,
                                            uint32_t sample_permille,
                                            auvcan_bit_timing_t *out)
{
    for (uint32_t p = 1u; p <= 512u; p++) {
        if (clk_hz % (p * bitrate) != 0u) {
            continue;
        }
        const uint32_t n = clk_hz / (p * bitrate);
        if (n < 8u || n > 80u) {
            continue;
        }
        const uint32_t sample_tq = (n * sample_permille + 500u) / 1000u;   /* includes sync */
        out->prescaler = p;
        out->seg1 = sample_tq - 1u;
        out->seg2 = n - sample_tq;
        out->sjw = out->seg2;
        return 0;
    }
    return -1;
}

#endif
