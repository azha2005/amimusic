/* adpcm.h - IMA ADPCM de 4 bits, el audio del disco de musica.
 *
 * Es el IMA/DVI de siempre, con dos detalles propios (docs/FORMAT.md):
 * el flujo es uno solo, con el estado arrancando en predictor 0 e indice 0
 * y siguiendo de muestra en muestra sin bloques; y lo que suena es el byte
 * alto del predictor de 16 bits, porque Paula es de 8 bits.
 */
#ifndef A5_ADPCM_H
#define A5_ADPCM_H

#include <stddef.h>
#include <stdint.h>

#define A5_ADPCM_STEPS 89

extern const int16_t a5_adpcm_step[A5_ADPCM_STEPS];
extern const int8_t  a5_adpcm_index[8];

typedef struct {
    int pred;       /* -32768..32767 */
    int index;      /* 0..88 */
} A5AdpcmState;

/* Aplica un nibble al estado (la regla del decoder) y devuelve el
 * predictor nuevo. */
int  a5_adpcm_step_nibble(A5AdpcmState *s, unsigned nibble);

/* Codifica n muestras de 16 bits (n par) en n/2 bytes, nibble alto primero.
 * lookahead 0: para cada muestra, el nibble que deja el predictor mas cerca.
 * lookahead 1: el que minimiza el error de esta muestra mas el de la
 * siguiente con su mejor nibble. recon (puede ser NULL) recibe lo que va a
 * sonar, en 8 bits. El estado se conserva entre llamadas. */
void a5_adpcm_encode(const int16_t *in, size_t n, uint8_t *out,
                     int8_t *recon, A5AdpcmState *s, int lookahead);

/* Decodifica nbytes (2 muestras por byte) a muestras de 8 bits. */
void a5_adpcm_decode(const uint8_t *in, size_t nbytes, A5AdpcmState *s,
                     int8_t *out);

#endif /* A5_ADPCM_H */
