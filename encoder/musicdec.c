/* musicdec.c - decoder de referencia del disco de musica (A5MU).
 *
 * Decodifica los datos exactamente como el reproductor y los verifica
 * contra el .crc del encoder: la tapa como la muestra Denise y el audio
 * como lo toca Paula. Da el WAV, el PPM de la tapa y un MP4 con las dos
 * cosas juntas.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "a500vp.h"
#include "stream.h"
#include "adpcm.h"
#include "ham.h"

#define A5M_HEADER_SIZE  32
#define A5M_AUDIO_ADPCM4 1
#define A5M_AUDIO_PCM8   2

static void die(const char *msg)
{
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}
static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *p;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = malloc(n > 0 ? (size_t)n : 1);
    if (!p || n < 0 || fread(p, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(p); return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return p;
}

static void put_le(FILE *f, uint32_t v, int bytes)
{
    while (bytes--) { fputc((int)(v & 0xFF), f); v >>= 8; }
}

static void write_wav(const char *path, const int8_t *s, size_t n, int rate)
{
    FILE *f = fopen(path, "wb");
    uint32_t datalen = (uint32_t)n * 2;
    size_t i;

    if (!f) die("no pude escribir el WAV");
    fwrite("RIFF", 1, 4, f); put_le(f, 36 + datalen, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put_le(f, 16, 4); put_le(f, 1, 2); put_le(f, 1, 2);
    put_le(f, (uint32_t)rate, 4); put_le(f, (uint32_t)rate * 2, 4);
    put_le(f, 2, 2); put_le(f, 16, 2);
    fwrite("data", 1, 4, f); put_le(f, datalen, 4);
    for (i = 0; i < n; i++) put_le(f, (uint16_t)(int16_t)(s[i] * 256), 2);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *in = NULL, *wav = NULL, *ppm = NULL, *preview = NULL;
    uint8_t *d, *crc, *rgb;
    size_t len, clen = 0, off, n, nbytes, q;
    int planes, period, afmt, i, bad = 0;
    int8_t *samples;
    A5Color pal[16], *disp;
    uint32_t crc_cover = 0;
    A5AdpcmState st = { 0, 0 };
    char crcpath[1024], tmpwav[1024];
    double hz;

    for (i = 1; i < argc; i++) {
        int has = i + 1 < argc;
        if (!strcmp(argv[i], "--in") && has)           in = argv[++i];
        else if (!strcmp(argv[i], "--wav") && has)     wav = argv[++i];
        else if (!strcmp(argv[i], "--ppm") && has)     ppm = argv[++i];
        else if (!strcmp(argv[i], "--preview") && has) preview = argv[++i];
        else {
            printf("uso: a5mu-dec --in <musica.a5m> [--wav <x.wav>] "
                   "[--ppm <tapa.ppm>] [--preview <x.mp4>]\n");
            return 2;
        }
    }
    if (!in) die("falta --in");

    d = slurp(in, &len);
    if (!d || len < A5M_HEADER_SIZE || memcmp(d, "A5MU", 4))
        die("no es un archivo A5MU");
    if (be16(d + 4) != 1) die("version de formato distinta");

    planes = d[8];
    period = (int)be16(d + 14);
    afmt = d[16];
    n = be32(d + 20);
    nbytes = be32(d + 24);
    hz = A5_CCK_PAL / period;
    if (planes && (planes != A5_HAM_PLANES || d[9] != 1 ||
                   be16(d + 10) != A5_HAM_W || be16(d + 12) != A5_HAM_H))
        die("tapa desconocida: solo HAM6 de 320x256");
    if (period < 124) die("periodo invalido");
    if (n & 1) die("cantidad de muestras impar");
    if (afmt == A5M_AUDIO_ADPCM4 ? nbytes != n / 2 :
        afmt == A5M_AUDIO_PCM8   ? nbytes != n : 1)
        die("formato o tamano de audio invalido");

    /* --- la tapa ---------------------------------------------------------- */
    off = A5M_HEADER_SIZE;
    disp = malloc(sizeof *disp * A5_HAM_W * A5_HAM_H);
    rgb = malloc((size_t)A5_HAM_W * A5_HAM_H * 3);
    if (!disp || !rgb) die("sin memoria");
    if (planes) {
        uint8_t *pix = malloc((size_t)A5_HAM_W * A5_HAM_H);
        if (!pix) die("sin memoria");
        if (off + 32 + A5_HAM_BYTES > len) die("la tapa esta incompleta");
        for (i = 0; i < 16; i++) pal[i] = (A5Color)be16(d + off + i * 2);
        a5_ham_depack(d + off + 32, pix);
        a5_ham_decode(pix, pal, disp);
        for (q = 0; q < (size_t)A5_HAM_W * A5_HAM_H; q++) {
            uint8_t wb[2];
            wb[0] = (uint8_t)(disp[q] >> 8);
            wb[1] = (uint8_t)disp[q];
            crc_cover = a5_crc32(wb, 2, crc_cover);
        }
        off += 32 + A5_HAM_BYTES;
        free(pix);
    } else {
        memset(disp, 0, sizeof *disp * A5_HAM_W * A5_HAM_H);
    }
    for (q = 0; q < (size_t)A5_HAM_W * A5_HAM_H; q++)
        a5_rgb444_to_srgb(disp[q], &rgb[q * 3], &rgb[q * 3 + 1],
                          &rgb[q * 3 + 2]);

    /* --- el audio --------------------------------------------------------- */
    if (off + nbytes > len) die("el archivo se corta antes del final del audio");
    samples = malloc(n ? n : 1);
    if (!samples) die("sin memoria");
    if (afmt == A5M_AUDIO_ADPCM4) a5_adpcm_decode(d + off, nbytes, &st, samples);
    else                          memcpy(samples, d + off, n);

    printf("datos      : %s (%lu bytes)\n", in, (unsigned long)len);
    printf("tapa       : %s\n", planes ? "HAM6 320x256" : "sin tapa");
    printf("audio      : %s, %lu muestras a %.3f Hz = %d:%05.2f\n",
           afmt == A5M_AUDIO_ADPCM4 ? "ADPCM 4 bits" : "pcm8",
           (unsigned long)n, hz, (int)(n / hz) / 60, fmod(n / hz, 60));

    snprintf(crcpath, sizeof crcpath, "%s.crc", in);
    crc = slurp(crcpath, &clen);
    if (crc && clen >= 8) {
        uint32_t want = be32(crc + 4), got = a5_crc32(samples, n, 0);
        if (be32(crc) != crc_cover) {
            printf("tapa       : CRC %08lX, el encoder decia %08lX\n",
                   (unsigned long)crc_cover, (unsigned long)be32(crc));
            bad++;
        }
        if (want != got) {
            printf("audio      : CRC %08lX, el encoder decia %08lX\n",
                   (unsigned long)got, (unsigned long)want);
            bad++;
        }
    } else {
        printf("(sin .crc: no se verifico contra el encoder)\n");
    }

    if (ppm) {
        FILE *f = fopen(ppm, "wb");
        if (!f) die("no pude escribir el PPM");
        fprintf(f, "P6\n%d %d\n255\n", A5_HAM_W, A5_HAM_H);
        fwrite(rgb, 1, (size_t)A5_HAM_W * A5_HAM_H * 3, f);
        fclose(f);
        printf("tapa       : %s\n", ppm);
    }
    if (wav) {
        write_wav(wav, samples, n, (int)(hz + 0.5));
        printf("wav        : %s\n", wav);
    }
    if (preview) {
        /* La tapa quieta, 5 cuadros por segundo, con el audio decodificado. */
        const double fps = 5;
        size_t frames = (size_t)ceil(n / hz * fps), fr;
        FILE *pre;
        snprintf(tmpwav, sizeof tmpwav, "%s.wav", preview);
        write_wav(tmpwav, samples, n, (int)(hz + 0.5));
        pre = a5_open_preview(preview, A5_HAM_W, A5_HAM_H, fps, 2, tmpwav,
                              0, 0, 1.0, (int)(hz + 0.5));
        if (!pre) die("no pude arrancar ffmpeg para el preview");
        for (fr = 0; fr < frames; fr++)
            fwrite(rgb, 1, (size_t)A5_HAM_W * A5_HAM_H * 3, pre);
        a5_pclose(pre);
        printf("preview    : %s\n", preview);
    }

    if (crc) {
        if (bad) { printf("\nVERIFICACION: FALLO\n"); return 1; }
        printf("\nVERIFICACION: OK. Tapa y audio coinciden exactamente con "
               "lo que simulo el encoder.\n");
    }
    free(d); free(crc); free(samples); free(disp); free(rgb);
    return 0;
}
