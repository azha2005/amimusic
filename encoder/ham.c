/* ham.c - la tapa en HAM6. Ver ham.h y docs/FORMAT.md. */
#include <stdlib.h>
#include <string.h>
#include "ham.h"

/* Un pixel HAM6: los bits 5-4 dicen que hacer con los 3-0. */
static A5Color ham_apply(A5Color prev, unsigned v, const A5Color pal[16])
{
    unsigned d = v & 15;

    switch (v >> 4) {
    case 0:  return pal[d];                                  /* paleta */
    case 1:  return (A5Color)((prev & 0xFF0) | d);           /* azul */
    case 2:  return (A5Color)((prev & 0x0FF) | (d << 8));    /* rojo */
    default: return (A5Color)((prev & 0xF0F) | (d << 4));    /* verde */
    }
}

void a5_ham_decode(const uint8_t *pix, const A5Color pal[16], A5Color *out)
{
    int x, y;

    for (y = 0; y < A5_HAM_H; y++) {
        A5Color c = pal[0];             /* cada linea arranca en el color 0 */
        for (x = 0; x < A5_HAM_W; x++) {
            c = ham_apply(c, pix[y * A5_HAM_W + x], pal);
            out[y * A5_HAM_W + x] = c;
        }
    }
}

void a5_ham_planarize(const uint8_t *pix, uint8_t *planes)
{
    int p, y, b, i;

    for (p = 0; p < A5_HAM_PLANES; p++)
        for (y = 0; y < A5_HAM_H; y++)
            for (b = 0; b < A5_HAM_ROWBYTES; b++) {
                unsigned v = 0;
                for (i = 0; i < 8; i++)
                    v = (v << 1) |
                        ((pix[y * A5_HAM_W + b * 8 + i] >> p) & 1u);
                planes[p * A5_HAM_PLANE_BYTES + y * A5_HAM_ROWBYTES + b] =
                    (uint8_t)v;
            }
}

void a5_ham_depack(const uint8_t *planes, uint8_t *pix)
{
    int p, y, x;

    memset(pix, 0, (size_t)A5_HAM_W * A5_HAM_H);
    for (p = 0; p < A5_HAM_PLANES; p++)
        for (y = 0; y < A5_HAM_H; y++)
            for (x = 0; x < A5_HAM_W; x++) {
                unsigned byte = planes[p * A5_HAM_PLANE_BYTES +
                                       y * A5_HAM_ROWBYTES + x / 8];
                pix[y * A5_HAM_W + x] |=
                    (uint8_t)(((byte >> (7 - (x & 7))) & 1u) << p);
            }
}

/* --- codificador ----------------------------------------------------------
 * Codicioso, HAM deja "flecos": para llegar a un color lejano hay que
 * cambiar las tres componentes de a una, y cada decision tomada sin mirar
 * adelante puede dejar la componente equivocada para el pixel siguiente.
 * Busqueda en haz por linea: en cada pixel se mantienen los `beam` caminos
 * mas baratos (error Oklab al cuadrado acumulado), uno por color final
 * distinto, porque lo que viene despues solo depende de ese color. Al final
 * de la linea se sigue hacia atras el mejor. */

typedef struct { float cost; uint16_t color, parent; } Beam;

/* Deja los k de menor costo en a[0..k-1] (Hoare, sin ordenar). */
static void select_k(Beam *a, int n, int k)
{
    int lo = 0, hi = n - 1;

    while (lo < hi) {
        float piv = a[(lo + hi) / 2].cost;
        int i = lo, j = hi;
        while (i <= j) {
            while (a[i].cost < piv) i++;
            while (a[j].cost > piv) j--;
            if (i <= j) { Beam t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (k - 1 <= j)      hi = j;
        else if (k - 1 >= i) lo = i;
        else                 break;
    }
}

void a5_ham_encode(const uint8_t *rgb, int beam, uint32_t seed,
                   A5Color pal[16], uint8_t *pix)
{
    static Oklab lab444[4096];
    static float ccost[4096];
    static uint16_t cpar[4096];
    static uint8_t ccode[4096];
    static int cstamp[4096];
    Oklab *tgt = malloc(sizeof *tgt * A5_HAM_W);
    Beam *cur = malloc(sizeof *cur * 4096), *nxt = malloc(sizeof *nxt * 4096);
    uint16_t *bpar;
    uint8_t *bcode;
    A5Palette p;
    A5Hist *h;
    int stamp = 0, x, y, i, c;

    if (beam < 1) beam = 1;
    if (beam > 4096) beam = 4096;
    bpar = malloc(sizeof *bpar * A5_HAM_W * (size_t)beam);
    bcode = malloc((size_t)A5_HAM_W * beam);
    if (!tgt || !cur || !nxt || !bpar || !bcode) {
        fprintf(stderr, "error: sin memoria\n");
        exit(1);
    }

    /* La paleta base: k-means en Oklab sobre la imagen, con el 0 negro. */
    h = a5_hist_new();
    a5_hist_add_frame(h, rgb, A5_HAM_W, 0, A5_HAM_H);
    a5_quantize(h, 16, 1, seed, &p);
    a5_hist_free(h);
    for (i = 0; i < 16; i++) pal[i] = i < p.n ? p.rgb444[i] : 0;

    for (c = 0; c < 4096; c++) {
        uint8_t r, g, b;
        a5_rgb444_to_srgb((A5Color)c, &r, &g, &b);
        lab444[c] = a5_srgb_to_oklab(r, g, b);
        cstamp[c] = -1;
    }

    for (y = 0; y < A5_HAM_H; y++) {
        const uint8_t *row = rgb + (size_t)y * A5_HAM_W * 3;
        int ncur = 1, best = 0;

        for (x = 0; x < A5_HAM_W; x++)
            tgt[x] = a5_srgb_to_oklab(row[x * 3], row[x * 3 + 1],
                                      row[x * 3 + 2]);
        cur[0].cost = 0;
        cur[0].color = pal[0];
        cur[0].parent = 0;

        for (x = 0; x < A5_HAM_W; x++) {
            int nn = 0, s;
            stamp++;
            for (s = 0; s < ncur; s++) {
                unsigned v;
                for (v = 0; v < 64; v++) {
                    A5Color nc = ham_apply(cur[s].color, v, pal);
                    float cost = cur[s].cost +
                                 a5_oklab_dist2(lab444[nc], tgt[x]);
                    if (cstamp[nc] != stamp) {
                        cstamp[nc] = stamp;
                        ccost[nc] = cost;
                        cpar[nc] = (uint16_t)s;
                        ccode[nc] = (uint8_t)v;
                        nxt[nn++].color = nc;
                    } else if (cost < ccost[nc]) {
                        ccost[nc] = cost;
                        cpar[nc] = (uint16_t)s;
                        ccode[nc] = (uint8_t)v;
                    }
                }
            }
            for (i = 0; i < nn; i++) {
                nxt[i].cost = ccost[nxt[i].color];
                nxt[i].parent = cpar[nxt[i].color];
            }
            if (nn > beam) { select_k(nxt, nn, beam); nn = beam; }
            for (i = 0; i < nn; i++) {
                bpar[(size_t)x * beam + i] = nxt[i].parent;
                bcode[(size_t)x * beam + i] = ccode[nxt[i].color];
            }
            { Beam *t = cur; cur = nxt; nxt = t; }
            ncur = nn;
        }

        for (i = 1; i < ncur; i++) if (cur[i].cost < cur[best].cost) best = i;
        for (x = A5_HAM_W - 1; x >= 0; x--) {
            pix[y * A5_HAM_W + x] = bcode[(size_t)x * beam + best];
            best = bpar[(size_t)x * beam + best];
        }
    }

    free(tgt); free(cur); free(nxt); free(bpar); free(bcode);
}
