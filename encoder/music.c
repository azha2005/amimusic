/* music.c - encoder del disco de musica (A5MU, docs/FORMAT.md).
 *
 * Lee la cancion con ffmpeg, la lleva a la frecuencia exacta de Paula y la
 * codifica en ADPCM de 4 bits o en pcm8; convierte la tapa a HAM6; escribe
 * los datos del disco y su .crc. El .adf lo arma M3.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "a500vp.h"
#include "stream.h"
#include "adpcm.h"
#include "ham.h"

#define A5M_MAGIC        "A5MU"
#define A5M_VERSION      1
#define A5M_HEADER_SIZE  32
#define A5M_AUDIO_ADPCM4 1
#define A5M_AUDIO_PCM8   2
#define A5M_COVER_BYTES  (16 * 2 + A5_HAM_BYTES)

/* Lo que queda en el disco para datos, aproximado: el disquete menos el
 * bootblock y un reproductor chico. El numero exacto lo da el ADF (M3). */
#define A5M_DISK_EST     883712

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

/* La tapa, encajada en 320x256. El pixel lowres PAL es 16/15 mas ancho que
 * alto: primero se angosta la imagen en esa proporcion y despues se la
 * encaja con barras negras (letterbox), recortando (crop) o estirando. */
static uint8_t *load_cover(const char *path, const char *fit)
{
    char vf[512];
    size_t sz = (size_t)A5_HAM_W * A5_HAM_H * 3;
    uint8_t *rgb = malloc(sz);
    FILE *f;

    if (!rgb) die("sin memoria");
    if (!strcmp(fit, "stretch"))
        snprintf(vf, sizeof vf, "scale=%d:%d:flags=lanczos,setsar=1",
                 A5_HAM_W, A5_HAM_H);
    else if (!strcmp(fit, "crop"))
        snprintf(vf, sizeof vf,
                 "scale=trunc(iw*15/16):ih,scale=%d:%d:"
                 "force_original_aspect_ratio=increase:flags=lanczos,"
                 "crop=%d:%d,setsar=1", A5_HAM_W, A5_HAM_H, A5_HAM_W, A5_HAM_H);
    else
        snprintf(vf, sizeof vf,
                 "scale=trunc(iw*15/16):ih,scale=%d:%d:"
                 "force_original_aspect_ratio=decrease:flags=lanczos,"
                 "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=black,setsar=1",
                 A5_HAM_W, A5_HAM_H, A5_HAM_W, A5_HAM_H);
    f = a5_open_decoder(path, 0, 0, vf, A5_HAM_W, A5_HAM_H);
    if (!f || fread(rgb, 1, sz, f) != sz) die("ffmpeg no pudo leer la tapa");
    a5_pclose(f);
    return rgb;
}

/* Error Oklab de lo que se ve contra la tapa original: medio y fraccion de
 * pixeles a mas de 0,1. */
static void cover_error(const uint8_t *rgb, const A5Color *disp,
                        double *mean, double *bad)
{
    size_t i, n = (size_t)A5_HAM_W * A5_HAM_H;
    double sum = 0;
    long nb = 0;

    for (i = 0; i < n; i++) {
        uint8_t r, g, b;
        double d;
        a5_rgb444_to_srgb(disp[i], &r, &g, &b);
        d = sqrt((double)a5_oklab_dist2(
            a5_srgb_to_oklab(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]),
            a5_srgb_to_oklab(r, g, b)));
        sum += d;
        if (d > 0.1) nb++;
    }
    *mean = sum / n;
    *bad = (double)nb / n;
}

static void usage(void)
{
    printf(
"uso: a5mu-enc --audio <cancion> [--cover <imagen>] [opciones]\n"
"\n"
"  --audio PATH        la cancion (cualquier cosa que lea ffmpeg)\n"
"  --cover PATH        la tapa (jpg, png...): se muestra en HAM6\n"
"  --cover-fit MODO    letterbox | crop | stretch (letterbox)\n"
"  --ham-beam N        caminos por linea al elegir los pixeles HAM (16)\n"
"  --audio-format F    adpcm | pcm8 (adpcm): pcm8 suena mejor y dura la\n"
"                      mitad\n"
"  --out PATH          datos del disco (work\\musica.a5m)\n"
"  --wav PATH          ademas, lo que va a sonar en un WAV\n"
"  --ppm PATH          ademas, la tapa como se va a ver (320x256)\n"
"  --start SEG         desde donde tomar la cancion (0)\n"
"  --duration SEG      cuanto tomar; 0 = toda (0)\n"
"  --period N          periodo de Paula (443 = 8006,5 Hz)\n"
"  --gain F|auto       ganancia; auto lleva el pico a -1 dB (auto)\n"
"  --lookahead 0|1     el encoder ADPCM mira una muestra adelante (1)\n"
"  --compare           mide tambien las otras codificaciones del audio\n");
}

int main(int argc, char **argv)
{
    const char *in = NULL, *out = "work\\musica.a5m", *wav = NULL;
    const char *cover = NULL, *fit = "letterbox", *ppm = NULL;
    double start = 0, duration = 0, gain = 0;     /* 0 = auto */
    int period = 443, lookahead = 1, compare = 0, beam = 16, i;
    int afmt = A5M_AUDIO_ADPCM4;
    double hz, step, peak = 0;
    int rate;
    int16_t *raw, *xs;
    size_t nraw, n, nbytes, k;
    uint8_t *abytes, *planes = NULL;
    int8_t *recon;
    A5Color pal[16];
    uint32_t crc_cover = 0;
    A5AdpcmState st = { 0, 0 };
    clock_t t0 = clock();

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int has = i + 1 < argc;
        if (!strcmp(a, "--audio") && has)          in = argv[++i];
        else if (!strcmp(a, "--cover") && has)     cover = argv[++i];
        else if (!strcmp(a, "--cover-fit") && has) fit = argv[++i];
        else if (!strcmp(a, "--ham-beam") && has)  beam = atoi(argv[++i]);
        else if (!strcmp(a, "--out") && has)       out = argv[++i];
        else if (!strcmp(a, "--wav") && has)       wav = argv[++i];
        else if (!strcmp(a, "--ppm") && has)       ppm = argv[++i];
        else if (!strcmp(a, "--start") && has)     start = atof(argv[++i]);
        else if (!strcmp(a, "--duration") && has)  duration = atof(argv[++i]);
        else if (!strcmp(a, "--period") && has)    period = atoi(argv[++i]);
        else if (!strcmp(a, "--lookahead") && has) lookahead = atoi(argv[++i]);
        else if (!strcmp(a, "--compare"))          compare = 1;
        else if (!strcmp(a, "--audio-format") && has) {
            const char *v = argv[++i];
            if (!strcmp(v, "adpcm"))     afmt = A5M_AUDIO_ADPCM4;
            else if (!strcmp(v, "pcm8")) afmt = A5M_AUDIO_PCM8;
            else die("--audio-format: adpcm o pcm8");
        } else if (!strcmp(a, "--gain") && has) {
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
    if (strcmp(fit, "letterbox") && strcmp(fit, "crop") && strcmp(fit, "stretch"))
        die("--cover-fit: letterbox, crop o stretch");

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
    abytes = malloc(n + 1);
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
        /* auto: el pico a -1 dB. Ni ADPCM ni pcm8 saturan por pendiente
         * como fib4, asi que conviene usar los 8 bits enteros. */
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

    if (afmt == A5M_AUDIO_ADPCM4) {
        a5_adpcm_encode(xs, n, abytes, recon, &st, lookahead);
        nbytes = n / 2;
    } else {
        for (k = 0; k < n; k++) {
            long v = lround(xs[k] / 256.0);
            recon[k] = (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v);
            abytes[k] = (uint8_t)recon[k];
        }
        nbytes = n;
    }

    printf("cancion    : %s\n", in);
    printf("audio      : %lu muestras a %.3f Hz (periodo %d) = %d:%05.2f\n",
           (unsigned long)n, hz, period, (int)(n / hz) / 60,
           fmod(n / hz, 60));
    printf("             ganancia %.2f (pico de la fuente %.0f de 32767)\n",
           gain, peak);
    if (afmt == A5M_AUDIO_ADPCM4)
        printf("ADPCM      : %lu bytes = %.2f KB/s, SNR %.1f dB (lookahead "
               "%d)\n", (unsigned long)nbytes, nbytes / 1024.0 / (n / hz),
               snr8(xs, recon, n), lookahead);
    else
        printf("pcm8       : %lu bytes = %.2f KB/s, SNR %.1f dB\n",
               (unsigned long)nbytes, nbytes / 1024.0 / (n / hz),
               snr8(xs, recon, n));

    if (compare) {
        /* Las alternativas, con exactamente el mismo audio de entrada. */
        uint8_t *tb = malloc(n + 1);
        int8_t *tr = malloc(n);
        float *xf = malloc(n * sizeof *xf);
        A5AdpcmState s2 = { 0, 0 }, s3 = { 0, 0 };
        int acc = 0;
        if (!tb || !tr || !xf) die("sin memoria");

        for (i = 0; i < 2; i++) {
            A5AdpcmState s = { 0, 0 };
            a5_adpcm_encode(xs, n, tb, tr, &s, i);
            printf("comparacion: ADPCM lookahead %d:  SNR %.1f dB, 4 bits\n",
                   i, snr8(xs, tr, n));
        }
        /* El mismo flujo con la salida redondeada en vez de truncada. */
        a5_adpcm_encode(xs, n, tb, NULL, &s2, lookahead);
        for (k = 0; k < n; k++) {
            unsigned nib = (k & 1) ? tb[k / 2] & 15 : tb[k / 2] >> 4;
            long v = lround(a5_adpcm_step_nibble(&s3, nib) / 256.0);
            tr[k] = (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v);
        }
        printf("             ADPCM, salida redondeada: SNR %.1f dB\n",
               snr8(xs, tr, n));
        for (k = 0; k < n; k++) xf[k] = xs[k] / 256.0f;
        a5_fib4_encode(xf, n, tb, tr, &acc);
        printf("             fib4 (A500VP):       SNR %.1f dB, 4 bits\n",
               snr8(xs, tr, n));
        for (k = 0; k < n; k++) {
            long v = lround(xf[k]);
            tr[k] = (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v);
        }
        printf("             pcm8:                SNR %.1f dB, 8 bits\n",
               snr8(xs, tr, n));
        free(tb); free(tr); free(xf);
    }

    /* --- la tapa ---------------------------------------------------------- */
    if (cover) {
        uint8_t *rgb = load_cover(cover, fit);
        uint8_t *pix = malloc((size_t)A5_HAM_W * A5_HAM_H);
        A5Color *disp = malloc(sizeof *disp * A5_HAM_W * A5_HAM_H);
        uint8_t wb[2];
        double mean, bad, pmean, pbad;
        clock_t tc = clock();

        planes = malloc(A5_HAM_BYTES);
        if (!pix || !disp || !planes) die("sin memoria");
        a5_ham_encode(rgb, beam, 1, pal, pix);
        a5_ham_decode(pix, pal, disp);
        a5_ham_planarize(pix, planes);
        cover_error(rgb, disp, &mean, &bad);

        /* Para comparar: la misma paleta de 16, sin HAM (cada pixel al
         * color de la paleta mas cercano). */
        {
            size_t q, np = (size_t)A5_HAM_W * A5_HAM_H;
            A5Color *pd = malloc(sizeof *pd * np);
            if (!pd) die("sin memoria");
            for (q = 0; q < np; q++) {
                Oklab t = a5_srgb_to_oklab(rgb[q * 3], rgb[q * 3 + 1],
                                           rgb[q * 3 + 2]);
                float bd = 1e30f;
                int c;
                for (c = 0; c < 16; c++) {
                    uint8_t r, g, b;
                    float d;
                    a5_rgb444_to_srgb(pal[c], &r, &g, &b);
                    d = a5_oklab_dist2(t, a5_srgb_to_oklab(r, g, b));
                    if (d < bd) { bd = d; pd[q] = pal[c]; }
                }
            }
            cover_error(rgb, pd, &pmean, &pbad);
            free(pd);
        }

        printf("tapa       : %s, %s, HAM6 320x256, busqueda de %d caminos "
               "(%.1f s)\n", cover, fit, beam,
               (double)(clock() - tc) / CLOCKS_PER_SEC);
        printf("             error Oklab %.4f, %.2f%% de los pixeles a mas "
               "de 0,1 de la original\n", mean, 100 * bad);
        printf("             sin HAM, solo los 16 colores: %.4f, %.2f%%\n",
               pmean, 100 * pbad);

        /* El CRC es de lo que se ve: el color de cada pixel, $0RGB. */
        {
            size_t q;
            for (q = 0; q < (size_t)A5_HAM_W * A5_HAM_H; q++) {
                wb[0] = (uint8_t)(disp[q] >> 8);
                wb[1] = (uint8_t)disp[q];
                crc_cover = a5_crc32(wb, 2, crc_cover);
            }
        }
        if (ppm) {
            FILE *f = fopen(ppm, "wb");
            size_t q;
            if (!f) die("no pude escribir el PPM");
            fprintf(f, "P6\n%d %d\n255\n", A5_HAM_W, A5_HAM_H);
            for (q = 0; q < (size_t)A5_HAM_W * A5_HAM_H; q++) {
                uint8_t r, g, b;
                a5_rgb444_to_srgb(disp[q], &r, &g, &b);
                fputc(r, f); fputc(g, f); fputc(b, f);
            }
            fclose(f);
            printf("             %s (como se va a ver)\n", ppm);
        }
        free(rgb); free(pix); free(disp);
    }

    {
        long room = A5M_DISK_EST - A5M_HEADER_SIZE -
                    (cover ? A5M_COVER_BYTES : 0);
        double bps = afmt == A5M_AUDIO_ADPCM4 ? hz / 2 : hz;
        printf("disco      : entran ~%d:%02d de audio%s; esta cancion ocupa "
               "%d:%02d\n", (int)(room / bps) / 60, (int)(room / bps) % 60,
               cover ? " con la tapa" : "", (int)(n / hz) / 60,
               (int)(n / hz) % 60);
        if ((double)nbytes > room)
            printf("  AVISO: no entra en un disco; sobran ~%.0f s (usa "
                   "--duration)\n", (nbytes - room) / bps);
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
        a5buf_put16(&b, 0);                         /* flags */
        a5buf_put8(&b, cover ? A5_HAM_PLANES : 0);  /* planos de la tapa */
        a5buf_put8(&b, cover ? 1 : 0);              /* modo: HAM */
        a5buf_put16(&b, cover ? A5_HAM_W : 0);
        a5buf_put16(&b, cover ? A5_HAM_H : 0);
        a5buf_put16(&b, (unsigned)period);
        a5buf_put8(&b, (unsigned)afmt);
        a5buf_put8(&b, 0);
        a5buf_put16(&b, 0);
        a5buf_put32(&b, (uint32_t)n);               /* muestras */
        a5buf_put32(&b, (uint32_t)nbytes);          /* bytes de audio */
        a5buf_put32(&b, 0);
        if (cover) {
            for (i = 0; i < 16; i++) a5buf_put16(&b, pal[i]);
            a5buf_write(&b, planes, A5_HAM_BYTES);
        }
        a5buf_write(&b, abytes, nbytes);

        f = fopen(out, "wb");
        if (!f || fwrite(b.p, 1, b.len, f) != b.len)
            die("no pude escribir la salida");
        fclose(f);

        snprintf(crcpath, sizeof crcpath, "%s.crc", out);
        f = fopen(crcpath, "wb");
        if (f) {
            uint8_t c[8];
            c[0] = (uint8_t)(crc_cover >> 24); c[1] = (uint8_t)(crc_cover >> 16);
            c[2] = (uint8_t)(crc_cover >> 8);  c[3] = (uint8_t)crc_cover;
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

    free(xs); free(recon); free(abytes); free(planes);
    return 0;
}
