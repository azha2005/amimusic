/* musicdec.c - decoder de referencia del disco de musica (A5MU).
 *
 * Decodifica los datos exactamente como el reproductor y los verifica
 * contra el .crc del encoder. Da el WAV de lo que va a sonar. La tapa
 * (M2) se salta por ahora.
 */
#include <stdlib.h>
#include <string.h>
#include "a500vp.h"
#include "stream.h"
#include "adpcm.h"

#define A5M_HEADER_SIZE  32
#define A5M_AUDIO_ADPCM4 1

static void die(const char *msg)
{
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
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
    const char *in = NULL, *wav = NULL;
    uint8_t *d, *crc;
    size_t len, clen = 0, off, n, nbytes, img;
    int planes, period, i, bad = 0;
    int8_t *samples;
    A5AdpcmState st = { 0, 0 };
    char crcpath[1024];

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--in") && i + 1 < argc)       in = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else {
            printf("uso: a5mu-dec --in <musica.a5m> [--wav <salida.wav>]\n");
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
    n = be32(d + 20);
    nbytes = be32(d + 24);
    if (d[16] != A5M_AUDIO_ADPCM4) die("formato de audio desconocido");
    if (period < 124) die("periodo invalido");
    if (n & 1 || nbytes != n / 2) die("cantidad de muestras invalida");

    /* La tapa: paleta de 16 colores y los planos (M2). */
    img = planes ? 32 + (size_t)planes * (be16(d + 10) / 8) * be16(d + 12) : 0;
    off = A5M_HEADER_SIZE + img;
    if (off + nbytes > len) die("el archivo se corta antes del final del audio");

    samples = malloc(n ? n : 1);
    if (!samples) die("sin memoria");
    a5_adpcm_decode(d + off, nbytes, &st, samples);

    printf("datos      : %s (%lu bytes)\n", in, (unsigned long)len);
    printf("tapa       : %s\n", planes ? "si" : "sin tapa");
    printf("audio      : ADPCM 4 bits, %lu muestras a %.3f Hz = %d:%05.2f\n",
           (unsigned long)n, A5_CCK_PAL / period,
           (int)(n / (A5_CCK_PAL / period)) / 60,
           (double)n / (A5_CCK_PAL / period) -
               60 * (int)(n / (A5_CCK_PAL / period) / 60));

    snprintf(crcpath, sizeof crcpath, "%s.crc", in);
    crc = slurp(crcpath, &clen);
    if (crc && clen >= 8) {
        uint32_t want = be32(crc + 4), got = a5_crc32(samples, n, 0);
        if (want != got) {
            printf("audio      : CRC %08lX, el encoder decia %08lX\n",
                   (unsigned long)got, (unsigned long)want);
            bad++;
        }
    } else {
        printf("(sin .crc: no se verifico contra el encoder)\n");
    }

    if (wav) {
        write_wav(wav, samples, n, (int)(A5_CCK_PAL / period + 0.5));
        printf("wav        : %s\n", wav);
    }
    if (crc) {
        if (bad) { printf("\nVERIFICACION: FALLO\n"); return 1; }
        printf("\nVERIFICACION: OK. El audio coincide exactamente con lo que "
               "simulo el encoder.\n");
    }
    free(d); free(crc); free(samples);
    return 0;
}
