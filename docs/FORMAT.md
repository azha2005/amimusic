# Formato de los discos de A5MU (y de A500VP, de donde viene)

Este fork define el **disco de musica (A5MU, version 1)**, en la seccion
siguiente. La imagen de disco, el bootblock y la cabecera del reproductor
son los de A500VP y siguen valiendo. El bitstream de video (version 4) queda
documentado mas abajo porque el codigo de video todavia esta en el repo,
pero el disco de musica no lo usa.

---

## Datos del disco de musica (A5MU, version 1)

Todo big-endian. Van donde irian los datos de video: en el primer sector
libre despues del reproductor.

### Cabecera (32 bytes)

| Offset | Tamano | Contenido |
|---|---|---|
| 0 | 4 | `"A5MU"` |
| 4 | 2 | Version = 1 |
| 6 | 2 | Flags, 0 |
| 8 | 1 | Planos de la tapa: 0 = sin tapa, 6 = HAM6 |
| 9 | 1 | Modo de la tapa: 1 = HAM (reservado mientras no haya tapa) |
| 10 | 2 | Ancho de la tapa en pixeles (320) |
| 12 | 2 | Alto de la tapa en lineas (256) |
| 14 | 2 | Periodo de Paula del audio |
| 16 | 1 | Formato del audio: 1 = IMA ADPCM de 4 bits, 2 = PCM de 8 bits con signo |
| 17 | 3 | Reservado, 0 |
| 20 | 4 | Muestras de audio (par) |
| 24 | 4 | Bytes de audio: muestras / 2 (ADPCM) o muestras (pcm8) |
| 28 | 4 | Reservado, 0 |

Despues, si hay tapa, la tapa; despues, el audio.

### Tapa: HAM6 de 320x256

16 palabras de paleta (`$0RGB`; la 0 es negro) y los 6 planos de 10 240
bytes, plano 0 primero, 40 bytes por linea, bit 7 = pixel de mas a la
izquierda. Son 61 472 bytes. Se muestra en lowres PAL, 256 lineas, con
`BPLCON0 = $6A00` (6 planos, HAM, color).

Cada pixel vale 6 bits; el plano 0 es el bit 0. Los bits 5-4 dicen que hacer
con los 3-0 (`d`), a partir del color del pixel anterior:

| bits 5-4 | color del pixel |
|---|---|
| 00 | `paleta[d]` |
| 01 | el anterior, con el azul = `d` |
| 10 | el anterior, con el rojo = `d` |
| 11 | el anterior, con el verde = `d` |

Cada linea arranca desde el color 0, que es tambien el del borde.

### Audio: PCM de 8 bits

Una muestra por byte, con signo, tal cual la toca Paula.

### Audio: IMA ADPCM de 4 bits

Dos muestras por byte, **nibble alto primero**. Es el IMA/DVI de siempre
(tablas de 89 pasos y de indices en `encoder\adpcm.c`), con un solo flujo
para toda la cancion: el estado arranca en **predictor 0, indice 0** y sigue
de muestra en muestra, sin bloques ni cabeceras intermedias. Por nibble:

```
paso = tabla_pasos[indice]
dif  = paso >> 3
si nibble & 4: dif += paso
si nibble & 2: dif += paso >> 1
si nibble & 1: dif += paso >> 2
predictor += (nibble & 8) ? -dif : dif      ; limitado a -32768..32767
indice    += tabla_indices[nibble & 7]      ; limitado a 0..88
muestra    = predictor >> 8                 ; aritmetico: lo que toca Paula
```

### Verificacion

El encoder escribe `<datos>.crc`: 8 bytes, dos CRC-32 big-endian. El de la
tapa es el de **lo que se ve**: el color `$0RGB` de cada pixel, linea por
linea, en big-endian (0 si no hay tapa); asi verifica tambien la regla de
HAM. El del audio es el de todas las muestras de 8 bits que tienen que
sonar, en orden. El decoder de referencia (`a5mu-dec`) recalcula los dos.

### Sector de medicion (sector 1759, offset 900 608)

El reproductor de medicion (`player/music.s` ensamblado con `-DBENCH=1`)
graba esto en el ultimo sector del disquete antes de quedarse quieto, para
que se pueda leer desde el PC con `a5mu-dec --measure <disco.adf>`. Todo en
long words big-endian.

| offset | contenido |
|---|---|
| 0 | `"MUSI"` |
| 4 | muestras de la cancion, segun la cabecera |
| 8 | **CRC-32 de las muestras que se le dieron a Paula** |
| 12 | buffers que pidio Paula, de 512 muestras |
| 16 | muestras que quedaron sin sumar al CRC (tiene que ser 0) |
| 20 | formato de audio |
| 24 | periodo de Paula |
| 28 | direccion del bloque 1 (slow RAM) |
| 32 | bytes del bloque 1 |
| 36 | direccion del bloque 2 (Chip) |
| 40 | bytes del bloque 2 |
| 44 | direccion de los bitplanes de la tapa |

El CRC del offset 8 es el mismo `a5_crc32` del `.crc`, y se calcula sobre
las muestras **reales**: el silencio con que se rellena el ultimo buffer no
entra. Por eso tiene que dar exactamente igual al CRC de audio del `.crc`.

El disco de verdad reserva ese sector, y ademas la diferencia de tamano
entre los dos reproductores, para que los mismos datos entren en los dos
discos y lo que se mide sea lo que se graba.

---

# Lo heredado de A500VP

**Version de formato del video: 4.** Define el disco, el arranque y el
bitstream de video y audio.

La version 2 no definia el contenido del audio de los paquetes (viajaba
vacio). La 3 lo define y usa el byte 28 de la cabecera, antes reservado,
para el formato del audio. La 4 agrega la paleta por franjas horizontales
(byte 29 de la cabecera) y cambia el tamano de la paleta de los paquetes.

Todo es **big-endian**. El 68000 lee el disco con punteros pelados, sin
conversiones.

Este archivo es la unica definicion del formato. El encoder
(`encoder\encode.c`, `encoder\stream.c`), el decoder de referencia
(`encoder\decode.c`) y el reproductor (`player\*.s`) lo siguen al pie de la
letra. Si cambia el formato, cambia la version.

La version 1 (delta por tramos "saltar N, escribir M" por plano) quedo
obsoleta; por que, con numeros, en `DECISIONS.md`.

---

## Imagen de disco

ADF de disquete DD: 80 cilindros x 2 caras x 11 sectores x 512 bytes =
**901 120 bytes**, 1760 sectores. Sin sistema de archivos AmigaDOS: el disco
no es un volumen valido y esta bien.

| Sectores | Contenido |
|---|---|
| 0–1 | Bootblock (1024 bytes) |
| 2 … | Reproductor, en sectores consecutivos |
| siguiente al reproductor … | Datos: bitstream (cabecera + paquetes) |
| 1680–1735 | Solo en discos de prueba: volcado del framebuffer y del copper list (Hito 3) |
| 1754–1758 | Solo en discos de medicion: tiempos de decodificacion (Hito 4) |
| 1759 | Solo en discos de prueba: sector de informacion (`MEMR`, `FBDM` o `PLAY`) |

Los datos empiezan en el primer sector libre despues del reproductor. `mkadf`
escribe en la cabecera del reproductor donde quedaron. En los discos de
prueba, `mkadf --reserve-tail` garantiza que los datos no pisen la zona
de volcado (80 sectores para el frame fijo, 6 para el de medicion); en el
disco final esos sectores son para datos.

---

## Bootblock (sectores 0–1)

| Offset | Tamano | Contenido |
|---|---|---|
| 0 | 4 | `"DOS"`, 0 |
| 4 | 4 | Checksum |
| 8 | 4 | Longitud del reproductor en bytes, multiplo de 512 |
| 12 | … | Codigo. El Kickstart salta aca |

El offset 8 es, en un disco AmigaDOS, el numero de bloque raiz. Como este
disco no es un volumen DOS, nadie lo mira: lo usamos para pasarle al codigo de
arranque cuanto tiene que leer. Entra en el checksum como cualquier otra
longword.

### Checksum

Suma de las 256 longwords big-endian del bloque, con el campo de checksum en
cero y **acarreo circular** (si la suma se desborda, se le suma 1). El valor
que se guarda en el offset 4 es el complemento a uno (NOT) de esa suma.

La comprobacion que hace el Kickstart es sumar las 256 longwords *incluido* el
checksum y exigir `$FFFFFFFF`. `adf_boot_valid()` implementa exactamente eso y
`mkadf` lo verifica antes de escribir el archivo. Se valido contra bootblocks
reales generados por `xdftool`.

### Que hace el codigo de arranque

Entra con `A1` = `IOStdReq` de trackdisk.device (unidad 0, ya abierto) y
`A6` = ExecBase; igual recarga ExecBase de `$4`.

1. Pone el borde en azul (senal de que arranco).
2. `AllocMem(longitud, MEMF_CHIP|MEMF_CLEAR)`. **Chip obligatorio:** en
   Kickstart 1.2/1.3 trackdisk solo lee a Chip RAM.
3. `CMD_READ` de esa longitud desde el offset 1024 (sector 2).
4. `TD_MOTOR` 0 para apagar el motor.
5. Pone el borde en verde y salta al reproductor con
   `A6` = ExecBase, `A1` = el mismo `IOStdReq`, `A0` = base del reproductor.

Si falta memoria o falla la lectura, pone el borde en rojo y se queda quieto.
El reproductor es **codigo independiente de posicion**.

---

## Cabecera del reproductor

El reproductor (`player\player.s`) empieza con 16 bytes fijos:

| Offset | Tamano | Contenido |
|---|---|---|
| 0 | 4 | `BRA.W` al codigo |
| 4 | 4 | `"A5PL"` |
| 8 | 4 | Offset en bytes, desde el principio del disco, donde empiezan los datos |
| 12 | 4 | Longitud de los datos en bytes |

`mkadf --data` exige el `"A5PL"` y escribe los dos campos. El reproductor
los lee PC-relativos y carga los datos el mismo, con el `IOStdReq` que le
pasa el bootblock.

---

## Bitstream de video

### Cabecera (32 bytes)

| Offset | Tamano | Contenido |
|---|---|---|
| 0 | 4 | `"A5VP"` |
| 4 | 2 | Version = 4 |
| 6 | 2 | Flags. Bit 0 = hay audio |
| 8 | 2 | Ancho logico = 160 |
| 10 | 2 | Alto logico = 128 |
| 12 | 1 | Bitplanes (1..4) |
| 13 | 1 | Colores = 2^bitplanes |
| 14 | 2 | Periodo de Paula del audio (0 = sin audio) |
| 16 | 2 | Primera fila logica activa (`y0`) |
| 18 | 2 | Ultima fila activa + 1 (`y1`) |
| 20 | 4 | Cantidad de paquetes = cantidad de frames |
| 24 | 4 | Bytes de paquetes que siguen a la cabecera |
| 28 | 1 | Formato del audio: 0 = ninguno, 1 = fib4, 2 = pcm8 |
| 29 | 1 | Filas logicas por franja de paleta (0 = una sola paleta) |
| 30 | 2 | Reservado, 0 |

Las filas fuera de `y0..y1-1` son las barras del letterbox: nunca cambian y
quedan en el color 0.

### Paquetes

Un paquete por frame, sin excepcion. **Cada paquete dura exactamente 2 VBL**
(1/24,960205 s). No hay paquetes de "repetir K frames": si hay que sostener
una imagen K frames, van K paquetes de repeticion. Asi el audio de cada frame
viaja en su propio paquete y la sincronia es trivial.

| Offset | Tamano | Contenido |
|---|---|---|
| 0 | 2 | Longitud total del paquete en bytes, incluido este campo. **Siempre par** |
| 2 | 1 | Operacion: 0 = DELTA, 1 = REPETICION |
| 3 | 1 | Flags. Bit 0 = trae paleta |
| 4 | 2 | Bytes de audio en este paquete |
| 6 | … | Paleta: `franjas` x `colores` x 2 bytes, RGB444 como `$0RGB`, franja 0 primero (si el bit 0 esta prendido) |
| … | … | Audio (los bytes indicados en el offset 4) |
| … | … | Delta (solo si la operacion es DELTA) |
| … | 0–1 | Relleno con cero hasta longitud par |

El audio va **antes** del video para que el reproductor lo encuentre en un
offset que se calcula sin recorrer el delta.

Como todos los paquetes tienen longitud par y prefijo de longitud, el cargador
puede recorrerlos y partir los datos en dos bloques de memoria en un limite de
paquete, sin mirar su contenido.

Un cambio de paleta siempre viaja con un DELTA: una REPETICION nunca trae
paleta.

### Paleta por franjas

El area activa se divide en franjas horizontales de `F` filas logicas (el
byte 29 de la cabecera), empezando en `y0`; la ultima puede ser mas corta.
`franjas = ceil((y1 - y0) / F)`, o 1 si `F` vale 0. Cada franja tiene su
paleta completa de `colores` entradas, y la fila logica `y` se ve con la
paleta de la franja `(y - y0) / F`. Las filas fuera de `y0..y1-1` usan la
franja 0 (son todas color 0).

- **El color 0 es el mismo en todas las franjas.** Es tambien el color del
  borde, y el Copper lo escribe una sola vez por frame; el decoder de
  referencia rechaza un paquete que no lo cumpla.
- **Con 4 bitplanes solo se admite una franja.** El Copper cambia los
  colores 1..n-1 de una franja al principio de su primera linea de
  pantalla, antes de que empiece la imagen: 7 colores entran de sobra, 15
  no (ver `DECISIONS.md`).
- Maximo 128 franjas (una por fila logica).

### Delta

| Tamano | Contenido |
|---|---|
| 16 | Mapa de filas: 128 bits, uno por fila logica |
| … | Por cada fila marcada, en orden ascendente: el bloque de fila |

Mapa de filas: el byte `i` cubre las filas `8i..8i+7`; el bit 7 es la fila
`8i`.

Bloque de fila:

| Tamano | Contenido |
|---|---|
| 3 | Mascara de columnas: 20 bits, uno por byte logico de la fila |
| … | Por cada columna marcada, en orden ascendente: `bitplanes` bytes, plano 0 primero |

Mascara de columnas: el bit 7 del primer byte es la columna 0 (pixeles
logicos 0..7), el bit 0 del segundo byte es la columna 15, y en el tercer
byte los bits 7..4 son las columnas 16..19. **Los bits 3..0 del tercer byte
tienen que valer 0**; el decoder de referencia rechaza el delta si no.

Cada byte de plano son 8 pixeles logicos, el bit 7 es el de mas a la
izquierda (como lo lee Denise).

Una columna marcada lleva **todos** los planos, aunque alguno no haya
cambiado: medido, en una fila modificada cambian en promedio 2,84 de 3
planos, y escribir el plano que sobra sale mas barato que describir cuales
cambiaron.

### Semantica (lo que hace el reproductor)

- Hay dos framebuffers planares, A y B, cada uno con su copper list. Al
  empezar los dos estan en cero (negro) y se ve A.
- **DELTA:** se escriben los bytes en el buffer **oculto**. Cada byte logico
  se expande a una palabra con la tabla de doblado de 256 entradas (cada
  pixel logico ocupa 2 pixeles de pantalla). Si el paquete trae paleta, se
  escribe en el copper list del buffer oculto. En el vertical blank se
  intercambian los buffers escribiendo `COP1LC`. Despues del intercambio la
  paleta se copia tambien al copper list del otro buffer, para que los dos la
  tengan.
- **REPETICION:** no se toca nada y no se intercambia: se sigue viendo el
  mismo buffer.
- El delta de un frame es contra el contenido del buffer oculto, que es el
  penultimo frame **distinto** mostrado (las repeticiones no intercambian).
- Cada fila logica se ve dos veces (doblado vertical por Copper; Hito 3).

### Audio

Mono. El reproductor lo toca igual por los canales 0 (izquierdo) y 1
(derecho) de Paula, con el periodo de la cabecera: la frecuencia es
**3 546 895 / periodo** Hz exactos (443 = 8006,535 Hz).

**El audio de todos los paquetes, puesto uno detras del otro, es un solo
flujo continuo.** El paquete `n` lleva las muestras que suenan mientras se
ve el frame `n`:

```
S(n) = 2 * floor((n + 1) * (Hz de Paula / 24,960205) / 2)
muestras del paquete n = S(n) - S(n-1),  S(-1) = 0
```

O sea, acumulacion fraccional con las frecuencias exactas, redondeada a
una cantidad **par** de muestras por paquete. El error acumulado es siempre
menor que 2 muestras (0,25 ms), dure lo que dure el video. Esta regla es
informativa: el reproductor no la necesita, solo consume el flujo.

- **fib4** (formato 1): Fibonacci-delta de 4 bits, el de 8SVX. Dos
  muestras por byte, **nibble alto primero**. Cada nibble indexa la tabla
  `-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21` y el valor
  se suma a un acumulador de 8 bits con signo; la muestra es el acumulador.
  El acumulador arranca en 0 al principio del flujo y **sigue de un paquete
  al otro**. La suma es en 8 bits con vuelta (como `add.b`), pero el
  encoder elige los nibbles para que nunca de la vuelta.
- **pcm8** (formato 2): una muestra por byte, con signo.

### Verificacion

El encoder escribe junto al bitstream un archivo `<salida>.crc`: un CRC-32
big-endian por frame, calculado sobre el mapa de indices visible
(160 x 128 bytes, un indice de paleta por pixel logico) seguido de las
paletas visibles de todas las franjas, en orden, serializadas en big-endian. El decoder de referencia recalcula lo
mismo a partir del bitstream y tiene que coincidir en **todos** los frames.

Si hay audio, despues del ultimo CRC de frame va uno mas: el CRC-32 de
todas las muestras que tienen que sonar (8 bits con signo, en orden).

---

## Sector de diagnostico del Hito 0 (sector 1759, offset 900 608)

Lo escribe el reproductor de prueba (`player\memcheck.s`) con `CMD_WRITE`
seguido de `CMD_UPDATE`, para poder leer la medicion desde el PC.

| Offset | Tamano | Contenido |
|---|---|---|
| 0 | 4 | `"MEMR"` |
| 4 | 4 | Chip libre |
| 8 | 4 | Chip, bloque contiguo mayor |
| 12 | 4 | No-chip (slow) libre |
| 16 | 4 | No-chip, bloque contiguo mayor |
| 20 | 4 | Total libre (chip + no-chip) |
| 24 | 4 | Direccion donde quedo cargado el reproductor |
| 28 | 4 | Direccion del bitplane |
| 32 | 480 | Cero |

Si el disquete esta protegido contra escritura la grabacion falla sin
consecuencias: el codigo de error de trackdisk se muestra en pantalla como
`SAVE ERR` (`$1C` = `TDERR_WriteProt`) y los valores se leen igual del monitor.

---

## Volcado del Hito 3 (sectores 1680–1759)

Lo escribe el reproductor de prueba (`player\still.s`) despues de
decodificar el primer paquete y armar su copper list, y antes de tomar el
hardware, con `CMD_WRITE` + `CMD_UPDATE`.

**Sectores 1680 en adelante:** el framebuffer tal cual esta en Chip RAM:
`bitplanes` x 5120 bytes (40 bytes por fila de pantalla x 128 filas), plano
0 primero. Son los bytes logicos ya doblados por la tabla. Con 3 planos
ocupa los sectores 1680–1709; con 4, hasta el 1719.

**Sectores 1720–1735:** el copper list, 8192 bytes (lo que se reserva; lo
que importa es el principio, hasta el `$FFFFFFFE` final).

**Sector 1759:**

| Offset | Tamano | Contenido |
|---|---|---|
| 0 | 4 | `"FBDM"` |
| 4 | 4 | Bitplanes |
| 8 | 4 | Direccion del framebuffer |
| 12 | 4 | Direccion del copper list |
| 16 | 4 | Bytes de delta que consumio el decodificador |
| 20 | 4 | Error de trackdisk al volcar el framebuffer (0 = bien) |
| 24 | 4 | Error de trackdisk al volcar el copper list (0 = bien) |
| 28 | 484 | Cero |

`a500vp-dec --still K --still-out <f>` genera el frame K como un solo DELTA
desde negro con sus paletas (`<f>`), el framebuffer esperado (`<f>.fb`) y la
imagen de referencia (`<f>.ppm`). `a500vp-dec --in <f> --check-still
<disco>` compara el volcado byte por byte: el framebuffer contra el frame
decodificado, y el copper list contra uno de referencia que el decoder arma
con la misma receta que `build_copper` + `write_palette`, franjas incluidas.
Es lo que corre `build.ps1 still`.

---

## Mediciones del Hito 4 (sectores 1754–1759)

Las graba el reproductor de medicion (`player.s` ensamblado con
`-DBENCH=1`) al terminar la reproduccion, despues de devolverle la maquina
al sistema.

**Sectores 1754–1758:** 640 longwords big-endian, una por paquete: los
color clocks (3 546 895 por segundo; la CPU hace 2 ciclos por cada uno) que
tardo en decodificarse ese DELTA, medidos con el haz de video (VBL, linea y
posicion horizontal). Cero para las repeticiones y para los paquetes
despues del 640.

**Sector 1759:**

| Offset | Tamano | Contenido |
|---|---|---|
| 0 | 4 | `"PLAY"` |
| 4 | 4 | Paquetes del bitstream |
| 8 | 4 | Duracion de la carga, en VSYNC (TOD del CIA-A) |
| 12 | 4 | Bytes cargados (cabecera + paquetes) |
| 16 | 4 | Direccion del bloque 1 (slow RAM) |
| 20 | 4 | Bytes de paquetes que quedaron en el bloque 1 |
| 24 | 4 | Direccion del bloque 2 (Chip) |
| 28 | 4 | Bytes de paquetes que quedaron en el bloque 2 |
| 32 | 4 | Frames que se mostraron tarde |
| 36 | 4 | Mayor atraso, en VBL |
| 40 | 4 | VBL que duro la reproduccion |
| 44 | 4 | VBL que tendria que haber durado (2 por paquete) |
| 48 | 8 | Direcciones de los dos framebuffers |
| 56 | 4 | Peor decodificacion, en color clocks |
| 60 | 4 | Paquete de la peor decodificacion |
| 64 | 4 | Deltas medidos |
| 68 | 4 | Entradas de la tabla de tiempos (640) |
| 72 | 4 | Error de trackdisk al grabar la tabla (0 = bien) |
| 76 | 4 | Interrupciones de audio (buffers que pidio Paula) |
| 80 | 8 | Estampa de la primera: VBL (4), linea (2), color clock (2) |
| 88 | 8 | Estampa de la ultima |
| 96 | 4 | VBL en que se ve el frame 0, en el mismo contador |
| 100 | 4 | Formato del audio (0 = sin audio) |
| 104 | 4 | Periodo de Paula |
| 108 | 4 | Color clocks llenando buffers de audio, sumados |
| 112 | 4 | El llenado mas largo, en color clocks |
| 116 | 396 | Cero |

Las estampas cuentan VBL con la interrupcion propia del reproductor; el
tiempo en color clocks es `VBL x 313 x 227 + linea x 227 + color clock`.
Los llenados se miden de la entrada a la salida de la rutina, sin la
entrada y salida de la interrupcion (unos 300 ciclos de CPU).

`a500vp-dec --in <bitstream> --measure <disco.adf>` los cruza con las
estadisticas de cada delta y ajusta el modelo de costo.
