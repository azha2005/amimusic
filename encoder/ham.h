/* ham.h - la tapa del disco de musica en HAM6 (docs/FORMAT.md).
 *
 * 320x256 lowres PAL, 6 bitplanes. Cada pixel es "uno de los 16 colores de
 * la paleta" o "el pixel anterior con el rojo, el verde o el azul
 * cambiado". Cada linea arranca desde el color 0, que es tambien el borde.
 */
#ifndef A5_HAM_H_INCLUDED
#define A5_HAM_H_INCLUDED

#include <stdint.h>
#include "a500vp.h"

#define A5_HAM_W           320
#define A5_HAM_H           256
#define A5_HAM_PLANES      6
#define A5_HAM_ROWBYTES    (A5_HAM_W / 8)
#define A5_HAM_PLANE_BYTES (A5_HAM_ROWBYTES * A5_HAM_H)          /* 10240 */
#define A5_HAM_BYTES       (A5_HAM_PLANES * A5_HAM_PLANE_BYTES)  /* 61440 */

/* Codifica rgb (rgb24, 320x256) en HAM6. pal recibe los 16 colores base
 * (el 0 es negro) y pix el valor de 6 bits de cada pixel. beam: caminos
 * que se mantienen por linea en la busqueda (1 = codicioso). */
void a5_ham_encode(const uint8_t *rgb, int beam, uint32_t seed,
                   A5Color pal[16], uint8_t *pix);

/* Lo que muestra Denise: el color RGB444 de cada pixel. */
void a5_ham_decode(const uint8_t *pix, const A5Color pal[16], A5Color *out);

/* Valores de 6 bits <-> 6 planos de 10240 bytes, plano 0 primero, bit 7 =
 * pixel de mas a la izquierda. */
void a5_ham_planarize(const uint8_t *pix, uint8_t *planes);
void a5_ham_depack(const uint8_t *planes, uint8_t *pix);

#endif /* A5_HAM_H_INCLUDED */
