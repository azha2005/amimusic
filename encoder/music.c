/* music.c - encoder del disco de musica (A5MU, docs/FORMAT.md).
 *
 * M1: el audio. Lee la cancion con ffmpeg, la lleva a la frecuencia exacta
 * de Paula, la codifica en ADPCM de 4 bits y escribe el archivo de datos,
 * su .crc y, si se pide, el WAV de lo que va a sonar. La tapa llega en M2.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "a500vp.h"
#include "stream.h"
#include "adpcm.h"

#define A5M_MAGIC        "A5MU"
#define A5M_VERSION      1
#define A5M_HEADER_SIZE  32
#define A5M_AUDIO_ADPCM4 1

/* Lo que queda en el disco para audio, aproximado: el disquete menos
 * bootblock y reproductor, menos una tapa HAM6 de 320x256 con su paleta.
 * El numero exacto lo va a dar el armado del ADF (M3). */
#define A5M_DISK_AUDIO_EST  (883712 - (6 * 40 * 256 + 32))

static void die(const char *msg)
{
    fprintf(stderr, "error: %s\n", msg);
    exit(1);
}

static void put_le(FILE *f, uint32_t v, int bytes)
{
    while (bytes--) { fputc((int)(v & 0xFF), f); v >>= 8; }
}

/* WAV de 16 bits mono: lo que va a sonar, para escucharlo en el PC. */
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

/* Todo el audio de la fuente, mono, s16 a rate Hz. */
static int16_t *read_audio(const char *in, double start, double dur,
                           int rate, size_t *n)
{
    FILE *f = a5_open_audio(in, start, dur, NULL, rate);
    int16_t *raw = NULL;
    size_t nraw = 0, cap = 0;
    uint8_t b2[2];

    if (!f) die("no pude arrancar ffmpeg para el audio");
    while (fread(b2, 1, 2, f) == 2) {
        if (nraw == cap) {
            cap = cap ? cap * 2 : 65536;
            raw = realloc(raw, cap * sizeof *raw);
            if (!raw) die("sin memoria");
        }
        raw[nraw++] = (int16_t)(b2[0] | (b2[1] << 8));    /* s16le */
    }
    a5_pclose(f);
    *n = nraw;
    return raw;
}

/* SNR de lo que suena (8 bits) contra la entrada, en la misma escala. */
static double snr8(const int16_t *x, const int8_t *got, size_t n)
{
    double sig = 0, noise = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        double r = x[i] / 256.0, e = r - got[i];
        sig += r * r;
        noise += e * e;
    }
    return noise > 0 ? 10 * log10(sig / noise) : 99;
}

static void usage(void)
{
    printf(
"uso: a5mu-enc --audio <cancion> [opciones]\n"
"\n"
"  --audio PATH        la cancion (cualquier cosa que lea ffmpeg)\n"
"  --out PATH          datos del disco (work\\musica.a5m)\n"
"  --wav PATH          ademas, lo que va a sonar en un WAV\n"
"  --start SEG         desde donde tomar la cancion (0)\n"
"  --duration SEG      cuanto tomar; 0 = toda (0)\n"
"  --period N          periodo de Paula (443 = 8006,5 Hz)\n"
"  --gain F|auto       ganancia; auto lleva el pico a -1 dB (auto)\n"
"  --lookahead 0|1     el encoder ADPCM mira una muestra adelante (1)\n"
"  --compare           mide tambien fib4 y pcm8 con el mismo audio\n");
}

int main(int argc, char **argv)
{
    const char *in = NULL, *out = "work\\musica.a5m", *wav = NULL;
    double start = 0, duration = 0, gain = 0;     /* 0 = auto */
    int period = 443, lookahead = 1, compare = 0, i;
    double hz, step, peak = 0;
    int rate;
    int16_t *raw, *xs;
    size_t nraw, n, k;
    uint8_t *abytes;
    int8_t *recon;
    A5AdpcmState st = { 0, 0 };
    clock_t t0 = clock();

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int has = i + 1 < argc;
        if (!strcmp(a, "--audio") && has)          in = argv[++i];
        else if (!strcmp(a, "--out") && has)       out = argv[++i];
        else if (!strcmp(a, "--wav") && has)       wav = argv[++i];
        else if (!strcmp(a, "--start") && has)     start = atof(argv[++i]);
        else if (!strcmp(a, "--duration") && has)  duration = atof(argv[++i]);
        else if (!strcmp(a, "--period") && has)    period = atoi(argv[++i]);
        else if (!strcmp(a, "--lookahead") && has) lookahead = atoi(argv[++i]);
        else if (!strcmp(a, "--compare"))          compare = 1;
        else if (!strcmp(a, "--gain") && has) {
            const char *v = argv[++i];
            gain = strcmp(v, "auto") ? atof(v) : 0;
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(); return 0;
        } else {
            fprintf(stderr, "opcion desconocida: %s\n", a);
            usage(); return 2;
        }
    }
    if (!in) { usage(); return 2; }
    if (period < 124 || period > 65535) die("periodo fuera de rango (124..65535)");

    /* --- audio a la frecuencia exacta de Paula -------------------------
     * ffmpeg solo entrega frecuencias enteras: se le pide la de arriba y el
     * ultimo paso es una interpolacion lineal (como en A500VP, Hito 5). */
    hz = A5_CCK_PAL / period;
    rate = (int)ceil(hz);
    raw = read_audio(in, start, duration, rate, &nraw);
    if (nraw < 2) die("ffmpeg no devolvio audio");
    step = rate / hz;
    n = (size_t)((nraw - 1) / step) & ~(size_t)1;       /* par */

    xs = malloc(n * sizeof *xs);
    recon = malloc(n);
    abytes = malloc(n / 2 + 1);
    if (!xs || !recon || !abytes) die("sin memoria");

    {
        double *x = malloc(n * sizeof *x);
        if (!x) die("sin memoria");
        for (k = 0; k < n; k++) {
            double pos = k * step, fr;
            size_t j = (size_t)pos;
            fr = pos - (double)j;
            x[k] = raw[j] + (raw[j + 1] - raw[j]) * fr;
            if (fabs(x[k]) > peak) peak = fabs(x[k]);
        }
        /* auto: el pico a -1 dB. ADPCM se adapta a la amplitud (no satura
         * por pendiente como fib4), asi que conviene usar los 8 bits. */
        if (gain <= 0) gain = peak > 0 ? 0.891 * 32767 / peak : 1;
        for (k = 0; k < n; k++) {
            long v = lround(x[k] * gain);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            xs[k] = (int16_t)v;
        }
        free(x);
    }
    free(raw);

    a5_adpcm_encode(xs, n, abytes, recon, &st, lookahead);

    printf("cancion    : %s\n", in);
    printf("audio      : %lu muestras a %.3f Hz (periodo %d) = %d:%05.2f\n",
           (unsigned long)n, hz, period, (int)(n / hz) / 60,
           fmod(n / hz, 60));
    printf("             ganancia %.2f (pico de la fuente %.0f de 32767)\n",
           gain, peak);
    printf("ADPCM      : %lu bytes = %.2f KB/s, SNR %.1f dB (lookahead %d)\n",
           (unsigned long)(n / 2), n / 2 / 1024.0 / (n / hz),
           snr8(xs, recon, n), lookahead);
    printf("             en un disco con tapa entran ~%d:%02d\n",
           (int)(A5M_DISK_AUDIO_EST * 2 / hz) / 60,
           (int)(A5M_DISK_AUDIO_EST * 2 / hz) % 60);

    if (compare) {
        /* Las alternativas, con exactamente el mismo audio de entrada. */
        uint8_t *tb = malloc(n / 2 + 1);
        int8_t *tr = malloc(n);
        float *xf = malloc(n * sizeof *xf);
        A5AdpcmState s2 = { 0, 0 };
        int acc = 0;
        if (!tb || !tr || !xf) die("sin memoria");

        a5_adpcm_encode(xs, n, tb, tr, &s2, !lookahead);
        printf("comparacion: ADPCM lookahead %d: SNR %.1f dB, mismo tamano\n",
               !lookahead, snr8(xs, tr, n));

        /* El mismo flujo, pero con la muestra de 8 bits redondeada en vez
         * de truncada (asr.w #8): cuanto ruido pone el truncado. */
        {
            A5AdpcmState s3 = { 0, 0 };
            for (k = 0; k < n; k++) {
                unsigned nib = (k & 1) ? abytes[k / 2] & 15 : abytes[k / 2] >> 4;
                long v = lround(a5_adpcm_step_nibble(&s3, nib) / 256.0);
                tr[k] = (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v);
            }
            printf("             ADPCM lookahead %d, salida redondeada: SNR "
                   "%.1f dB\n", lookahead, snr8(xs, tr, n));
        }
        for (k = 0; k < n; k++) xf[k] = xs[k] / 256.0f;
        a5_fib4_encode(xf, n, tb, tr, &acc);
        printf("             fib4 (A500VP):      SNR %.1f dB, mismo tamano\n",
               snr8(xs, tr, n));
        for (k = 0; k < n; k++) {
            long v = lround(xf[k]);
            tr[k] = (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v);
        }
        printf("             pcm8:               SNR %.1f dB, el doble de "
               "bytes (el techo de Paula)\n", snr8(xs, tr, n));
        if (wav) {                      /* para compararlo de oido */
            char p[1024];
            snprintf(p, sizeof p, "%s.pcm8.wav", wav);
            write_wav(p, tr, n, (int)(hz + 0.5));
            printf("             pcm8 en %s\n", p);
        }
        free(tb); free(tr); free(xf);
    }

    /* --- datos del disco ------------------------------------------------ */
    {
        A5Buf b;
        FILE *f;
        char crcpath[1024];
        uint32_t crc_audio = a5_crc32(recon, n, 0);

        a5buf_init(&b);
        a5buf_write(&b, A5M_MAGIC, 4);
        a5buf_put16(&b, A5M_VERSION);
        a5buf_put16(&b, 0);                     /* flags */
        a5buf_put8(&b, 0);                      /* planos de la tapa: sin */
        a5buf_put8(&b, 0);                      /* modo de la tapa */
        a5buf_put16(&b, 0);                     /* ancho */
        a5buf_put16(&b, 0);                     /* alto */
        a5buf_put16(&b, (unsigned)period);
        a5buf_put8(&b, A5M_AUDIO_ADPCM4);
        a5buf_put8(&b, 0);
        a5buf_put16(&b, 0);
        a5buf_put32(&b, (uint32_t)n);           /* muestras */
        a5buf_put32(&b, (uint32_t)(n / 2));     /* bytes de audio */
        a5buf_put32(&b, 0);
        a5buf_write(&b, abytes, n / 2);

        f = fopen(out, "wb");
        if (!f || fwrite(b.p, 1, b.len, f) != b.len)
            die("no pude escribir la salida");
        fclose(f);

        snprintf(crcpath, sizeof crcpath, "%s.crc", out);
        f = fopen(crcpath, "wb");
        if (f) {
            uint8_t c[8] = { 0, 0, 0, 0 };      /* tapa: 0 = sin tapa */
            c[4] = (uint8_t)(crc_audio >> 24); c[5] = (uint8_t)(crc_audio >> 16);
            c[6] = (uint8_t)(crc_audio >> 8);  c[7] = (uint8_t)crc_audio;
            fwrite(c, 1, 8, f);
            fclose(f);
        }
        printf("salida     : %s (%lu bytes) y su .crc\n", out,
               (unsigned long)b.len);
        a5buf_free(&b);
    }
    if (wav) {
        write_wav(wav, recon, n, (int)(hz + 0.5));
        printf("wav        : %s\n", wav);
    }
    printf("tiempo     : %.1f s\n", (double)(clock() - t0) / CLOCKS_PER_SEC);

    free(xs); free(recon); free(abytes);
    return 0;
}
