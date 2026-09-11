/* adpcm.c - IMA ADPCM de 4 bits. Ver adpcm.h y docs/FORMAT.md. */
#include "adpcm.h"

const int16_t a5_adpcm_step[A5_ADPCM_STEPS] = {
        7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
       19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
       50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
      337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
      876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
     2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
     5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

const int8_t a5_adpcm_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

int a5_adpcm_step_nibble(A5AdpcmState *s, unsigned nibble)
{
    int step = a5_adpcm_step[s->index];
    int diff = step >> 3;

    if (nibble & 4) diff += step;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 1) diff += step >> 2;
    s->pred += (nibble & 8) ? -diff : diff;
    if (s->pred > 32767)  s->pred = 32767;
    if (s->pred < -32768) s->pred = -32768;
    s->index += a5_adpcm_index[nibble & 7];
    if (s->index < 0)  s->index = 0;
    if (s->index > 88) s->index = 88;
    return s->pred;
}

/* Lo que suena: el byte alto del predictor (desplazamiento aritmetico,
 * como asr.w #8 en el 68000). */
static int8_t out8(int pred)
{
    return (int8_t)(pred >= 0 ? pred >> 8 : -((-pred + 255) >> 8));
}

static double sq(double v) { return v * v; }

void a5_adpcm_encode(const int16_t *in, size_t n, uint8_t *out,
                     int8_t *recon, A5AdpcmState *s, int lookahead)
{
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned best = 0, k;
        double bd = 1e300;

        for (k = 0; k < 16; k++) {
            A5AdpcmState t = *s;
            double d = sq(in[i] - a5_adpcm_step_nibble(&t, k));
            if (lookahead && i + 1 < n && d < bd) {
                /* El mejor nibble siguiente desde este estado. */
                double d2 = 1e300;
                unsigned k2;
                for (k2 = 0; k2 < 16; k2++) {
                    A5AdpcmState u = t;
                    double e = sq(in[i + 1] - a5_adpcm_step_nibble(&u, k2));
                    if (e < d2) d2 = e;
                }
                d += d2;
            }
            if (d < bd) { bd = d; best = k; }
        }
        a5_adpcm_step_nibble(s, best);
        if (recon) recon[i] = out8(s->pred);
        if (i & 1) out[i / 2] |= (uint8_t)best;
        else       out[i / 2] = (uint8_t)(best << 4);
    }
}

void a5_adpcm_decode(const uint8_t *in, size_t nbytes, A5AdpcmState *s,
                     int8_t *out)
{
    size_t i;

    for (i = 0; i < nbytes; i++) {
        *out++ = out8(a5_adpcm_step_nibble(s, in[i] >> 4));
        *out++ = out8(a5_adpcm_step_nibble(s, in[i] & 15));
    }
}
