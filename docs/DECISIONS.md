# Decisiones de diseno

Una entrada por decision no obvia, con fecha, metodo y resultado. Las
mediciones reemplazan a las estimaciones de `CLAUDE.md`; cuando algo
contradice ese documento, se dice aca.

---

## 2026-09-10 — Toolchain: se reusa el vbcc que ya estaba

**Decision:** usar `C:\Users\JC\vbcc\bin\vasmm68k_mot.exe` (vasm 1.9) y el gcc
de MSYS2/UCRT64, ambos ya instalados en la maquina para el proyecto
`amiga260k`.

**Motivo:** no tiene sentido tener dos ensambladores. Se llego a compilar vasm
2.0f desde el fuente antes de encontrar el que ya estaba; se comprobo que
ambos producen bytes identicos para `player\boot.s` y se descarto el propio.
`tools\get-vasm.ps1` queda como plan B documentado.

---

## 2026-09-10 — Kickstart 1.2, no 1.3

**Decision:** el objetivo de desarrollo pasa a ser Kickstart **1.2** (33.180).

**Motivo:** es la ROM de la maquina real de Az, que es la que manda. `CLAUDE.md`
pedia 1.3; 1.2 es mas viejo, asi que lo que funcione en 1.2 funciona en 1.3.

**Consecuencia concreta:** `AvailMem(MEMF_TOTAL)` es **V36 (Kickstart 2.0)** y
no existe en 1.2/1.3. La "memoria total" que pedia el Hito 0 se mide entonces
como *libre* (suma de bloques) y *bloque contiguo mas grande*
(`MEMF_LARGEST`), que ademas es lo que de verdad importa para presupuestar.

---

## 2026-09-10 — Memoria libre en la maquina objetivo (Hito 0)

**Metodo:** el bootblock carga un stage2 de 1548 bytes desde el sector 2 y
salta a el. Lo primero que hace stage2, antes de reservar nada mas, son cuatro
llamadas a `AvailMem`. WinUAE configurado como A500 PAL OCS, 68000
cycle-exact, KS 1.2, 512 KB chip + 512 KB slow, cero fast.

En ese momento ya estan reservados el buffer del bootblock y los 2048 bytes de
stage2, que es lo correcto: el reproductor tambien va a estar residente cuando
cargue los datos.

| | bytes | KB | usado por el sistema |
|---|---|---|---|
| Chip libre | 499 224 | 487,5 | 25 064 |
| Chip, bloque contiguo mayor | 499 224 | 487,5 | (sin fragmentar) |
| Slow libre (`MEMF_FAST`) | 499 200 | 487,5 | 25 088 |
| Slow, bloque contiguo mayor | 499 176 | 487,5 | |
| **Total libre** | **998 424** | **975,0** | |

stage2 quedo en `$000059E8` y el bitplane en `$000061E8`.

**Conclusion:** hay bastante mas memoria de la que estimaba `CLAUDE.md`
(~850–900 KB). El presupuesto real **no lo limita la RAM sino el disquete**:

- disco: 901 120 − 1024 (bootblock) − reproductor ≈ **898 KB**
- RAM: 975 KB

Con lo cual `--budget` se calcula desde el disco. Ademas, como ningun bloque
contiguo pasa de 487,5 KB, los datos se van a repartir igual en dos bloques
(uno en slow, uno en chip), tal como preveia el diseno.

---

## 2026-09-10 — Frecuencia exacta del VBL PAL

**Decision:** VBL = **49,920409 Hz**, video a **24,960205 fps** (1 frame cada
2 VBL).

**Metodo:** el reloj de color PAL de la Amiga es 3 546 895 Hz exactos
(portadora PAL 4 433 618,75 Hz × 4/5). Un frame PAL **no entrelazado** tiene
**313 lineas** de 227 color clocks:

```
3 546 895 / (313 × 227) = 3 546 895 / 71 051 = 49,920409 Hz
```

WinUAE confirma las dos cosas en su barra de estado durante la prueba del
Hito 0: muestra `313p` y `FPS: 49.9`.

Es decir: no son 50 Hz, y tampoco 49,92 redondos. Un opening de 30 s son
748,8 frames de video. Este valor va parametrizado en el encoder, no escrito a
mano en varios lados.

Para el audio, con `periodo = 443` la frecuencia real de Paula es
3 546 895 / 443 = **8006,535 Hz**, que es la que tiene que usar el encoder para
la acumulacion fraccional de muestras por frame.

---

## 2026-09-10 — El reproductor graba la medicion en el disquete

**Decision:** ademas de mostrarla en pantalla, stage2 escribe la medicion en el
**ultimo sector del disco (1759)** con `CMD_WRITE` + `CMD_UPDATE`.

**Motivo:** que el resultado del Hito 0 se pueda leer desde el PC en vez de
depender de una foto de la pantalla. `build.ps1 check` arranca WinUAE, lo
cierra y lee ese sector del `.adf`. Es una medicion automatizada y repetible,
no un "se ve bien".

**Trampa encontrada:** WinUAE mantiene la pista escrita en memoria. Si se lo
mata con `Kill()` el `.adf` queda sin cambios y parece que la Amiga no grabo
nada. Hay que cerrarlo con `CloseMainWindow()`. En la Amiga hace falta
`CMD_UPDATE` despues del `CMD_WRITE` por el mismo motivo: trackdisk tambien
bufferea la pista.

Este sector es solo de diagnostico del Hito 0. Cuando entren los datos de
video (Hito 2) hay que decidir si se conserva o se libera; son 512 bytes.

---

## 2026-09-10 — Hito 1: la fuente no necesita telecine inverso

**Metodo:** `ffprobe` sobre *Fullmetal Alchemist Brotherhood OP 1_2.mp4*.

1280x720, SAR 1:1, DAR 16:9, `field_order=progressive`, **24000/1001 =
23,976024 fps** reales, 92,69 s, audio AAC 44100 Hz estereo.

**Decision:** se toma 1:1, sin `fieldmatch,decimate`. Es una fuente
progresiva de 23,976, no un telecine de 29,97.

La aceleracion PAL queda en **+4,10 %**: 24,960205 / 23,976024 = 1,041044.
Los primeros 30 s de fuente (720 frames) duran **28,846 s** en la Amiga.

---

## 2026-09-10 — El letterbox son 96 filas, no 90

**Decision:** para una fuente 16:9 el area activa es **160x96**, con 32 filas
de barra negra (16 arriba, 16 abajo).

**Motivo:** `CLAUDE.md` estimaba "unas 90 filas". El numero exacto sale de la
relacion de aspecto del pixel. La pantalla de 320x256 se ve en un tubo 4:3, o
sea que un pixel de pantalla (y por lo tanto un pixel logico, que son 2x2) es
1,0667 veces mas ancho que alto: la SAR 16:15 de PAL lowres.

```
filas = 160 x 1,0667 / (16/9) = 96
```

Comprobacion: 96 filas logicas son 192 lineas de pantalla; 192/256 x 3 = 2,25;
4 / 2,25 = 1,7778 = 16:9 exacto.

El calculo se hace en el encoder y no con `force_original_aspect_ratio` de
ffmpeg, justamente porque ffmpeg supondria pixeles cuadrados.

---

## 2026-09-10 — Oklab en vez de CIE Lab

**Decision:** el espacio perceptual del encoder es **Oklab**.

**Motivo:** `CLAUDE.md` pedia "Lab o similar". Oklab no necesita elegir blanco
de referencia, es mas barato de calcular y se porta bastante mejor con azules
saturados, que en un opening de anime sobran. Se usa para el k-means, para
elegir el color de paleta mas cercano y para medir el error.

---

## 2026-09-10 — Negro reservado y deduplicacion de paleta

**Decision:** con letterbox, el indice 0 de la paleta queda fijo en `$000` y
el k-means corre con n-1 centroides.

**Motivo:** las barras ocupan el 25 % de la pantalla y tienen que ser negro
exacto; el color 0 es ademas el fondo del Copper. Si se dejara al k-means,
saldria un gris oscuro y las barras se verian sucias.

**Correccion que hizo falta:** al redondear los centroides a RGB444, dos
centroides distintos pueden caer en el mismo color. Con el negro reservado
pasaba seguido: la escena del fuego terminaba con `000 000 ...`, o sea 7
colores utiles en vez de 8. Ahora, si un color de la paleta queda repetido, se
lo reemplaza por el color de la escena peor representado. El error medio bajo
de 0,0414 a 0,0405.

---

## 2026-09-10 — Deteccion de cortes por distancia Oklab media

**Decision:** hay corte de escena cuando la distancia Oklab media entre un
frame y el anterior (solo sobre el area activa) supera 0,12, con un minimo de
6 frames por escena.

**Motivo:** se mide sobre el frame ya escalado a 160x96, que es exactamente lo
que se va a cuantizar. Medirlo sobre la fuente en HD detectaria cosas que a
160 px de ancho no existen.

**Resultado en los primeros 30 s:** 15 escenas, error medio de cuantizacion
0,0405 en Oklab. Y un dato que importa para el Hito 2: **el 45,6 % de los
frames son casi identicos al anterior** (distancia < 0,008). Es lo que va a
comprimir el comando de repeticion; el anime animado "en dos" se nota.

---

## 2026-09-10 — Realce despues de escalar, y lo que cuesta

**Decision:** opcion `--sharpen F` (apagada por defecto) que mete un
`unsharp=3:3:F:3:3:0` **despues** de escalar.

**Motivo:** el anime vive de la linea negra de contorno. A 160 px de ancho,
partiendo de 1280, esa linea es sub-pixel y el escalado la promedia hasta
hacerla desaparecer: las caras quedan planas. Realzar despues de escalar la
devuelve. Antes de escalar no serviria de nada.

**El costo, medido:** con `--sharpen 1.2` los frames casi identicos bajan de
45,6 % a 40,5 %. El realce agrega detalle de alta frecuencia, y ese detalle
cambia entre frames. **Imagen mas nitida = mas bytes de delta.** Hay que
volver a medirlo en el Hito 2 contra el presupuesto real.

---

## 2026-09-10 — Cadencia nativa en vez de aceleracion PAL

**Esto contradice a `CLAUDE.md`, a pedido de Az.** El documento decia: "los
frames de la fuente se toman 1:1 [...] el audio se remuestrea para durar
exactamente lo mismo (con el cambio de tono, como en la TV PAL)". Az escucho
el preview y el audio acelerado le molesto.

**Decision:** por defecto `--rate native`. Se repite un frame cada tanto para
que la velocidad sea la de la fuente y **el audio no se toca**. `--rate pal`
mantiene el comportamiento viejo.

**Como funciona:** la Amiga solo puede cambiar de frame cada 2 VBL, o sea
24,960205 fps clavados. En vez de estirar el tiempo, se mapea cada hueco de
pantalla al frame de fuente que le toca:

```
frame_fuente(hueco) = redondeo(hueco * 23,976024 / 24,960205)
```

Cuando el indice no avanza, sale un comando de repeticion.

**Lo que cuesta, medido:** los primeros 30 s pasan de 720 a **750 huecos, de
los cuales 30 son repeticiones** (4,1 %). En bytes de video eso es casi nada,
porque el comando de repeticion ya existia para el anime animado en dos: los
frames casi identicos suben de 40,5 % a 42,9 %. Lo unico que crece de verdad
es el audio, que ahora dura 30,03 s en vez de 28,85 s: a ~4 KB/s son unos
**4,7 KB mas**. Es una ganga.

**Residuo:** 750 huecos son 30,048 s contra 30,030 s de fuente, 18 ms de
diferencia en 30 s (0,06 %). El audio se estira eso, que es una milesima de
semitono: inaudible.

**Preview:** va a 24,960205 fps con el audio original en mono, que es lo que
va a salir por Paula. En `--rate pal` el audio se acelera con
`asetrate` + `aresample` y no con `atempo`, porque la TV PAL sube la velocidad
**y** el tono, y el preview tiene que sonar como va a sonar.

---

## 2026-09-11 — Hito 2: bitstream v2, mascara de columnas en vez de tramos

**Decision:** el delta describe cada fila modificada con una **mascara de 20
bits** (una por byte logico) y, por cada columna marcada, manda **todos** los
planos. Reemplaza al formato v1 que proponia `CLAUDE.md` como punto de
partida: por fila y por plano, tramos "saltar N, escribir M".

**Metodo:** se codifico el opening (30 s, 8 colores, ajustado al presupuesto)
con v1 y el decoder de referencia desgloso a donde iban los bytes:

| v1 | bytes | % |
|---|---|---|
| cabeceras de tramo (2 bytes cada una) | 333 636 | 37,9 |
| cuenta de tramos por fila-plano | 78 914 | 9,0 |
| mascara de planos por fila | 27 762 | 3,2 |
| datos literales | 428 972 | 48,7 |

Los tramos promediaban **2,57 bytes**: se pagaban 2 bytes de cabecera por
cada 2,57 de dato. Y en una fila modificada cambiaban **2,84 de 3 planos**,
casi siempre en las mismas columnas. Describir por separado que cambio en
cada plano era tirar bytes.

**Resultado, v2 con la misma fuente y el mismo presupuesto (883 712 bytes):**

| | v1 | v2 |
|---|---|---|
| 8 colores, error final (Oklab) | 0,0708 | **0,0471** |
| 16 colores, error final | 0,1067 | **0,0763** |
| Mascara de columnas / datos | — | 11,7 % / 86,8 % |

Con 8 colores v2 ademas deja 84 KB sin usar (ver el punto sobre el control
de tasa, mas abajo). El decoder de referencia reconstruye los 750 frames de
las dos variantes bit a bit igual que el encoder.

**Costo de decodificacion:** el lazo del 68000 pasa a recorrer 20 bits por
fila en vez de interpretar tramos. Con el modelo (sin calibrar) el peor frame
queda en 39,8 ms con 8 colores y 49,5 ms con 16.

---

## 2026-09-11 — La repeticion usa el mismo criterio que la calidad

**Decision:** un frame se repite si, aplicando el filtro de calidad contra el
buffer **visible** (con un umbral 1,5 veces mas permisivo, `--repeat-boost`),
no queda ningun pixel que valga la pena actualizar.

**Motivo:** al principio la repeticion tenia un umbral propio y fijo (error
medio < 0,010). Al subir la perdida para entrar en el presupuesto, el buffer
visible se alejaba cada vez mas del frame ideal y las repeticiones caian de
~50 % a **3,3 %**, justo cuando mas falta hacian. Con el criterio unificado,
ajustado al presupuesto, 8 colores repite el 45 % de los frames y 16 colores
el 56 %.

---

## 2026-09-11 — Bug en la regla que descarta filas enteras

La calidad con perdida descartaba una fila si la suma de errores que quedaban
**dividida por 160** era chica. Eso hacia parecer despreciable un cambio
grande concentrado en tres pixeles (3 x 1,0 / 160 = 0,019) y se tiraban filas
con detalle real. Ahora se compara la suma sin dividir. Los streams salen mas
grandes a igual umbral, pero porque ya no se pierde lo que no se debia perder.

---

## 2026-09-11 — Histeresis temporal en el cuantizador

**Decision:** al cuantizar un frame, si el indice que tenia el pixel en el
frame anterior esta a menos de `--stability` (Oklab) del optimo, se lo deja.
Default **0,10**.

**Motivo:** la fuente es un h264 de 2 Mbit/s. Su ruido de compresion no se
ve a 160 px de ancho, pero hace que en zonas quietas los pixeles salten entre
dos colores vecinos de la paleta, y cada salto se paga en bytes de delta. Con
16 colores es peor, porque los colores de la paleta estan mas cerca entre si.

**Medido** (16 colores, v1, casi sin perdida): 2,05 MB sin histeresis,
1,91 MB con 0,03, 1,84 MB con 0,06, **1,41 MB con 0,10**. El error final
paso de 0,0844 a 0,0921: un 31 % menos de bytes por casi nada.

---

## 2026-09-11 — El denoise temporal empeora las cosas

Se probo `hqdn3d` antes de escalar (`--denoise`), para limpiar el ruido de
la fuente. **Los streams salieron mas grandes**: de 1,49 a 1,56 MB con 8
colores y de 2,33 a 2,39 MB con 16. El filtrado temporal convierte el ruido
en derivas lentas que igual cruzan los limites entre colores de la paleta, y
la histeresis ya hacia ese trabajo mejor. La opcion queda, apagada por
defecto.

---

## 2026-09-11 — 16 colores contra 8 con el presupuesto del disquete

Az eligio 16 colores en el Hito 1 mirando el preview **sin comprimir**, donde
16 se ve claramente mejor (error de cuantizacion 0,0318 contra 0,0446). Con
el bitstream real y el disquete como limite, la cuenta cambia:

| 30 s, 883 712 bytes | 8 colores | 16 colores |
|---|---|---|
| Tamano casi sin perdida | 1,63 MB | 2,31 MB |
| Perdida necesaria para entrar | umbral 0,207 | umbral 0,271 |
| Frames repetidos | 45 % | 56 % |
| Error final contra su ideal | **0,0471** | 0,0763 |
| Peor frame, modelo sin calibrar | 39,8 ms | 49,5 ms |

Un 4.o bitplane son 33 % mas bytes por columna, y con un solo disquete eso se
paga en perdida y en frames sostenidos (movimiento mas entrecortado).

**Pero ninguna de las dos variantes es aceptable asi.** Ver la entrada
siguiente: los errores de esta tabla son medias, y la media escondia un
salpicado muy visible.

---

## 2026-09-11 — La perdida por pixel produce salpicado: no sirve a este nivel

**Hallazgo:** mirando los previews decodificados (no las estadisticas), los
streams ajustados al disquete tienen **pixeles viejos desparramados** por
toda la imagen: restos de la escena anterior sobre la cara de un personaje,
estela en el fuego. El control, el mismo opening casi sin perdida (1,63 MB),
sale limpio: el formato y el decoder estan bien, y el decoder verifica bit a
bit contra el encoder. El problema es la estrategia de perdida.

**Por que:** para meter 1,63 MB en ~880 KB, el control de tasa subio el
umbral por pixel hasta 0,207 en Oklab. Eso quiere decir "no actualices
ningun pixel cuyo color este a menos de 0,2 del correcto", y 0,2 es
muchisimo. Cada pixel decide solo, sin mirar a sus vecinos, y el resultado
es ruido sal-y-pimienta que ademas persiste frame tras frame.

**Leccion de metodo:** el error *medio* (0,047) parecia razonable y no lo
era. El encoder ahora informa tambien el **porcentaje de pixeles activos con
error visible (> 0,1)**, que es lo que el ojo ve. Ningun parametro se da por
bueno sin mirar el preview decodificado.

**Consecuencia:** hay que reducir ~1,9x de otra manera. Opciones a medir y
decidir con Az: tope de cadencia (`--min-hold`, el anime esta animado en dos),
perdida coherente por regiones en vez de por pixel, clip mas corto.

---

## 2026-09-11 — Tope de cadencia (`--min-hold`): medido

**Que es:** la imagen se actualiza como mucho cada N huecos de pantalla
(N = 2 son hasta 12,5 fps de imagenes distintas; N = 3, hasta 8,3). Los
cortes de escena pasan siempre. El anime esta animado mayormente "en dos",
asi que buena parte de esos frames ya eran repetidos.

**Medido** (8 colores, primeros 30 s, realce 1,2):

| `--min-hold` | casi sin perdida | ajustado a 883 712: pixeles con error visible |
|---|---|---|
| 1 | 1 633 634 bytes | 27,2 % |
| 2 | 1 087 810 bytes | 18,1 % |
| 3 | **833 820 bytes** | 6,0 % |

Con N = 3 el opening entra en 883 KB **sin perdida**. Su 6 % de "error
visible" no es salpicado: es el retraso de sostener una imagen hasta 3
huecos en las zonas que se mueven, o sea movimiento mas entrecortado. La
metrica no distingue esos dos casos; el ojo si.

**Correccion de presupuesto:** esos 883 KB eran solo video. El audio del
Hito 5 (fib4, 4 bits, periodo 443 = 8006,5 Hz) son **4003 bytes/s**:
~120 KB en 30 s. El presupuesto real de video para 30 s es ~763 KB, y ahi
ya ni N = 3 entra sin perdida.

---

## 2026-09-11 — Decision de Az: 22 s a 12,5 fps, 8 colores, sin perdida

**Lo que no entra:** 30 s a 25 fps, casi sin perdida, son 1,63 MB. El disco
deja ~763 KB de video una vez descontado el audio. Achicar 2x con perdida
por pixel produce salpicado inaceptable (ver arriba).

**Opciones que se le presentaron a Az, todas medidas y miradas:**

| | cadencia | perdida | pixeles con error visible |
|---|---|---|---|
| 30 s | hasta 8,3 fps | umbral 0,124 | 13,2 %, salpicado visible |
| 20 s | hasta 12,5 fps | ninguna | 2,3 %, limpio |
| 24 s | hasta 12,5 fps | umbral 0,109 | 3,8 % |

Mas "invertir en el encoder" (resultado incierto) y "dos disquetes" (fuera
del alcance de la V1 segun `CLAUDE.md`).

**Decision:** Az eligio **~22 s a 12,5 fps**. Con eso:

- **8 colores, no 16.** Revierte lo que Az eligio en el Hito 1 mirando el
  preview sin comprimir. Con un solo disquete no hay caso: hasta el caso mas
  holgado ocuparia ~1 MB con 16 colores contra ~800 KB disponibles.
- `--min-hold 2`: la imagen cambia como mucho cada 2 huecos. El anime esta
  animado en dos, asi que se nota solo en los paneos.
- `--stability 0.07`: ver la entrada siguiente.
- Primeros 22 s del opening: 528 frames de fuente, 550 huecos, 22,035 s.

**Resultado final del Hito 2:** 774 048 bytes de video contra 795 499 de
presupuesto (sobran 21 KB), **sin ninguna perdida** (umbral base 0,012), 54 %
de repeticiones, 11 cambios de paleta, 1407 bytes por frame de media y 6086
el peor. Peor frame segun el modelo sin calibrar: 39,8 ms. El decoder de
referencia verifica los 550 frames.

---

## 2026-09-11 — Histeresis 0,07: el punto donde el zoom no se raya

La histeresis del cuantizador (`--stability`) es la que mas bytes ahorra,
pero en un zoom o un fundido "pega" los colores viejos. Con 0,10 el primer
plano de los anteojos (12,5 s) sale con manchas blancas pegadas en la cara.

Medido y mirado (20 s, `--min-hold 2`):

| `--stability` | casi sin perdida | zoom de los anteojos |
|---|---|---|
| 0 | 908 KB | limpio |
| 0,05 | 791 KB | limpio |
| 0,07 | (22 s: 774 KB) | limpio |
| 0,10 | 634 KB | **manchas pegadas** |

0,07 es lo mas alto que se probo sin el defecto, y es lo que permite los 22 s
sin perdida. Default del encoder.

---

## 2026-09-11 — El presupuesto por defecto ya descuenta el audio

Olvidar el audio al presupuestar paso una vez. Ahora, si no se pasa
`--budget`, el encoder calcula el video disponible como el disco menos el
audio que se va a llevar la duracion real en pantalla, a fib4 (4 bits por
muestra) y 3546895/`--audio-period` Hz. Para 22 s: 883 712 − 88 213 =
**795 499 bytes**.

*Reemplazado en el Hito 5:* el audio viaja dentro de los paquetes y el
presupuesto es uno solo (ver "Presupuesto unico" mas abajo).

---

## 2026-09-11 — El control de tasa deja presupuesto sin usar

La busqueda binaria sobre el umbral de calidad supone que mas umbral siempre
da menos bytes, y en promedio es asi, pero no de forma suave: una repeticion
de mas cambia el contenido del buffer oculto para todos los frames que
siguen, y el tamano salta. Con 8 colores la busqueda termino en 799 KB contra
un tope de 883 KB: **84 KB (10 %) sin usar** que podrian ir a calidad. Hay
que mejorarlo (por ejemplo, un ajuste fino por escena despues de la busqueda
global).

---

## 2026-09-11 — Hito 3: doblado vertical por modulo, no por punteros

**Decision:** el Copper duplica cada fila logica alternando `BPL1MOD` y
`BPL2MOD` entre −40 y 0 en cada linea de pantalla. `CLAUDE.md` proponia
reescribir los punteros de bitplane cada dos lineas y marcaba como duda el
momento exacto del WAIT horizontal, con un framebuffer de 256 filas como
alternativa si no salia estable.

**Motivo:** el modulo se suma a los punteros al terminar el fetch de la
linea, cerca de hpos $D8. Con −40, la linea siguiente vuelve a leer la misma
fila; con 0 avanza. Escribirlo al principio de la linea (WAIT a hpos $06)
deja **casi una linea entera de margen**: la escritura solo tiene que caer
entre el fin del fetch de la linea anterior y el fin del de esta, ~200 color
clocks. Reescribir punteros exige caer en el blanking horizontal, entre
DDFSTOP y DDFSTRT, y son 6 escrituras por par de lineas con 3 planos (12 con
4) contra 2 por linea. No hizo falta el alternativo de 256 filas.

**Costo:** 256 lineas x (WAIT + 2 MOVE) = 3 KB de copper list por
framebuffer, en Chip.

**Verificado en WinUAE (A500, 68000 cycle-exact, KS 1.2):** el disco de
prueba (`build.ps1 still`) carga el frame 182 de `final22.a5v` como un DELTA
desde negro y lo muestra fijo. El framebuffer que dejo el decodificador en
ensamblador es **identico byte a byte** al del decoder de referencia (15 360
bytes), la paleta coincide y consumio exactamente los 6064 bytes del delta.
En la captura la imagen sale entera, en bloques de 2x2, con la proporcion y
los colores de la referencia. Queda confirmar en la A500 real (Hito 7).

---

## 2026-09-11 — Capturas: PrintWindow, nunca copiar la pantalla

El primer intento de captura copiaba los pixeles de la pantalla en el
rectangulo de la ventana de WinUAE, despues de pedirle a Windows que la
trajera al frente. Windows no deja que un proceso de fondo robe el foco, asi
que se capturo **otra aplicacion que estaba encima**. Esa captura se borro.

Ahora `build.ps1` y `tools\shot.ps1` usan `PrintWindow` con
`PW_RENDERFULLCONTENT`: la ventana de WinUAE se dibuja sola en un bitmap. No
depende de que haya encima y no puede capturar nada que no sea el emulador.

---

## 2026-09-11 — Bug: leer IO_ERROR a traves de A1 despues de DoIO

`memcheck.s` (Hito 0) leia `IO_ERROR(a1)` despues de `DoIO`. Por la
convencion de exec, `a0/a1/d0/d1` no se preservan en una llamada: funcionaba
por casualidad. Ahora se lee a traves de `a5`, que guarda el `IOStdReq` todo
el programa. Salio al escribir el volcado del Hito 3, que hace lo mismo.

---

## 2026-09-11 — Hito 4: carga y reproduccion medidas

**Metodo:** disco de medicion (`build.ps1 play`). El reproductor ensamblado
con `-DBENCH=1` carga `final22.a5v` (22 s, 774 048 bytes), lo reproduce
entero, le devuelve la maquina al sistema y graba en el disquete lo que
midio (formato en `FORMAT.md`). La carga se cronometra con el TOD del CIA-A
(cuenta VSYNC y solo se lee); cada decodificacion, con el haz de video: VBL
contados por la interrupcion propia, linea y posicion horizontal, con
resolucion de un color clock. WinUAE A500, 68000 cycle-exact, KS 1.2,
512 KB chip + 512 KB slow, disquetera al 100 %.

| | medido |
|---|---|
| Carga | 774 048 bytes en 42,1 s = **17,9 KB/s** (2103 VSYNC) |
| Bloque 1 (slow RAM, `$C04A18`) | 497 318 bytes de paquetes |
| Bloque 2 (Chip, `$0135E8`) | 276 698 bytes de paquetes |
| Reproduccion | **1100 VBL para 1100 esperados**: sin deriva |
| Frames mostrados tarde | 1, por 2 VBL |
| Decodificacion media | 34,9 ms por delta |
| Peor decodificacion | **60,3 ms** (frame 541: 96 filas, 1920 columnas, pantalla entera) |

**Carga:** 17,9 KB/s, dentro de lo que estimaba `CLAUDE.md` (15–25 KB/s).
Con el audio del Hito 5 (88 KB mas) la espera antes del video va a ser de
~47 s. Hay margen barato: hoy la CPU copia cada byte del rebote al bloque
(~2,4 s en total) sin que el disco lea mientras tanto; con lecturas
asincronicas se solaparia. El trackloader propio que `CLAUDE.md` deja como
opcional es la mejora grande.

**El modelo de costo estaba mal por 1,6x.** El contado a mano predecia en
promedio el 61 % de lo medido. Ajuste por minimos cuadrados sobre los 253
deltas:

```
ciclos = 5580 + 851,6 x filas + 177,5 x columnas     R2 = 1,0000
```

con residuo maximo de 0,14 ms. El R2 perfecto no es casualidad: el hardware
emulado cycle-exact es determinista y el costo es de verdad lineal en filas
y columnas. Lo que la cuenta a mano no veia: recorrer los 20 bits de la
mascara cuesta ~36 ciclos por columna aunque no este marcada, 720 por fila.
Si alguna vez hace falta, se puede saltar de a 8 columnas cuando un byte de
la mascara vale 0. Las constantes nuevas estan en `encoder\stream.h`.

**El limite de 40 ms por frame no es la regla que importa.** Un frame
completo tarda 60 ms y aun asi hubo un solo frame tarde. El reproductor
empieza a decodificar un delta cuando el intercambio anterior libera el
buffer oculto, y tiene hasta el VBL de ese delta: con `--min-hold 2` son
80 ms, salvo en los cortes de escena, que pueden venir pegados. Los 40 ms de
`CLAUDE.md` son el peor caso de dos deltas seguidos, no un limite por frame.
Ver la entrada siguiente.

---

## 2026-09-11 — El encoder simula la linea de tiempo del reproductor

**Decision:** en vez de un tope fijo de 40 ms por frame, el encoder simula
lo que hace el reproductor. Cada delta empieza a decodificarse cuando el
intercambio anterior libera el buffer oculto y tiene que estar listo para el
VBL en que le toca verse; con el modelo calibrado, el encoder sabe cuanto
tarda. Si un delta llegaria tarde por mas de `--max-late` VBL (2 por
defecto), se degrada; si no, se lo deja llegar tarde. `--frame-ms` queda
como tope fijo opcional, apagado por defecto.

**Esto se aparta de `CLAUDE.md`**, que pedia degradar o repartir todo frame
que pasara de 40 ms. Motivos, los dos medidos:

1. 40 ms no es el limite real, es el caso de dos deltas seguidos. Con
   `--min-hold 2` cada delta tiene 80 ms, salvo en los cortes de escena.
2. Degradar es subir el umbral de perdida por pixel, y eso deja salpicado
   persistente (Hito 2). Un frame que se ve 40 ms tarde, con la sincronia
   intacta, se nota mucho menos. Con el modelo calibrado y el tope fijo de
   40 ms se habrian degradado todos los frames de pantalla completa del
   opening, sin necesidad.

**Validacion:** con el modelo calibrado, la simulacion sobre `final22.a5v`
predice **1 frame tarde, por 2 VBL** (el 548, un corte de escena pegado a
otro delta): exactamente lo que midio la Amiga. Con `--max-late 2` no se
degrada nada y el stream sale identico byte a byte al que se midio. El peor
delta previsto, 60,5 ms, coincide con el medido, 60,3 ms.

"Repartir un frame en los siguientes", la otra opcion de `CLAUDE.md`, sigue
sin implementar: con estos numeros no hace falta.

---

## 2026-09-11 — Hito 5: el audio es un solo flujo continuo (formato v3)

**Decision:** el audio de todos los paquetes, puesto uno detras del otro, es
un unico flujo. El paquete `n` lleva las muestras `S(n-1)..S(n)-1`, con
`S(n) = 2 floor((n+1) x Hz_Paula / 24,960205 / 2)`: acumulacion fraccional
con las frecuencias exactas, redondeada a una cantidad par. En fib4 el
acumulador arranca en 0 y **sigue de un paquete al otro**. La cabecera gana
un byte de formato (offset 28: 0 ninguno, 1 fib4, 2 pcm8) y la version pasa
a 3. Todo en `FORMAT.md`.

**Motivos:**

- Muestras pares por paquete: en fib4 un paquete termina en un byte entero,
  y en el reproductor no hace falta llevar la cuenta de medio byte.
- Acumulador continuo: si se reiniciara en cada paquete, cada 40 ms habria
  un salto al centro, que suena como un zumbido de 25 Hz.
- El lector de audio del reproductor camina los paquetes por su cuenta,
  independiente del video. Si un frame llega tarde, el audio no se entera:
  la politica que pedia `CLAUDE.md` sale gratis.
- El error acumulado por el redondeo a par es siempre menor que 2 muestras
  (0,25 ms), dure lo que dure el video.

El encoder escribe en el `.crc` un CRC mas, el de todas las muestras que
tienen que sonar. El decoder de referencia decodifica el audio del bitstream
como el reproductor, lo compara (**176 424 muestras identicas** en el opening
de 22 s) y lo mezcla en el preview: lo que se oye en `preview.mp4` es fib4 de
verdad, no la fuente.

---

## 2026-09-11 — Remuestreo fino en C, no en ffmpeg

ffmpeg solo entrega frecuencias enteras, y Paula con periodo 443 toca a
8006,535 Hz. Pedirle 8006 o 8007 dejaria una deriva de 1,5 ms por minuto que
no hace falta. El encoder le pide a ffmpeg la entera de arriba (8007 Hz, con
su filtro antialias, que es bueno) y hace el ultimo paso con interpolacion
lineal a la frecuencia **exacta**. La razon es 1,00006: la interpolacion casi
no filtra y la duracion cuadra al microsegundo con la del video.

Con `--rate pal` (video 4 % mas rapido) el audio se lee mas rapido y sube de
tono como en la TV; se le pide a ffmpeg 8006,5/1,041 Hz para que el
antialias corte donde corresponde despues de acelerar. Con la cadencia
`native` que eligio Az no se toca.

---

## 2026-09-11 — fib4: la ganancia optima es 1, y subirla empeora

El opening llega a pico 38 de 127 en 8 bits (−10 dB). Lo obvio seria
normalizar, pero fib4 no puede subir mas de 21 ni bajar mas de 34 por
muestra: con mas amplitud, los agudos fuertes no alcanzan (sobrecarga de
pendiente). Medido sobre los 22 s, SNR de lo que suena contra la entrada:

| ganancia | pico | SNR fib4 | SNR pcm8 |
|---|---|---|---|
| 0,5 | 19 | 19,7 dB | |
| 0,85 | 33 | 20,8 dB | |
| **1,0** | 38 | **20,9 dB** | 28,4 dB |
| 1,25 | 48 | 20,8 dB | |
| 2,0 | 77 | 17,8 dB | |
| 3,3 | 126 | 12,1 dB | 38,8 dB |

Meseta entre 0,85 y 1,25; queda **1,0** (`--audio-gain`). pcm8 a 3,3 suena
mucho mejor (38,8 dB) pero ocupa el doble: 176 KB en vez de 88, o sea 88 KB
menos de video (11 %). Queda como opcion (`--audio-format pcm8
--audio-gain 3.3`); es una decision de Az, no tecnica.

El codificador fib4 es codicioso (el mejor nibble para cada muestra, sin
mirar adelante). Uno con busqueda hacia adelante daria varios dB mas al
mismo tamano, sin tocar el formato ni el reproductor: candidato para el
Hito 6.

---

## 2026-09-11 — Presupuesto unico y el disco lo arma el encoder

**Decision:** `--budget` cuenta cabecera mas paquetes, **audio incluido**. El
audio no se negocia con el control de tasa: se codifica primero y el video
se ajusta a lo que queda. Con `--adf`, el encoder arma el disco entero
(bootblock, reproductor, datos) y el presupuesto por defecto es exactamente
lo que queda en el disco despues del reproductor, no una estimacion.

La memoria no limita: en el Hito 0 se midieron 975 KB libres entre Chip y
slow. Descontando framebuffers, copper lists, buffers de Paula, rebote,
reproductor y el margen de Chip quedan ~915 KB para datos, contra 872 KB de
disco.

`build.ps1 disk` reserva al final del disco lo que necesita el disco de
medicion para usar **el mismo** `video.a5v`: su reproductor ocupa 7 sectores
mas (tablas de tiempos) y graba 6 sectores de resultados. Son 13 sectores,
6,5 KB. Para el disco final se podria recuperar, pero entonces lo que se
mide no es lo que se graba.

Resultado con el opening de 22 s: 889 856 bytes de presupuesto, 88 212 de
audio y 862 380 usados. Sobran 27 KB: la busqueda del control de tasa
arranca en `--quality 8` y solo sabe subir la perdida, y a esa calidad ya
entra. Aprovechar ese resto es trabajo del Hito 6.

---

## 2026-09-11 — Hito 5: audio en el reproductor y sincronia medida

**Diseno.** Dos buffers de 512 muestras en Chip (64 ms cada uno a
8006,5 Hz). Paula toca uno mientras la interrupcion de nivel 4 (AUD0) llena
el otro y lo encola en `AUD0LC`/`AUD1LC`: los dos canales tocan el mismo
buffer, uno de cada lado. El lector de audio recorre los paquetes por su
cuenta, con su propio puntero (pasa del bloque 1 al 2 en el mismo limite que
el video), y de cada paquete solo mira los bytes de audio. Cuando el flujo se
acaba repite la ultima muestra: silencio. El DMA de audio arranca dentro de
la interrupcion de VBL del frame 0. El filtro pasabajos queda encendido.

Al empezar a llenar, el nivel 4 baja la prioridad a 2 (`move.w #$2200,sr`):
llenar dura 4,5 ms, y sin eso el VBL se atrasaria y el intercambio de copper
list podria caer ya dentro de la pantalla.

**Medido** con el disco de medicion (WinUAE A500 cycle-exact, KS 1.2):

| | |
|---|---|
| Buffers pedidos por Paula | 345, exactamente los que caben en 1100 VBL |
| Primer buffer | 0,12 ms despues del VBL del frame 0 |
| Intervalo medio | 226 814,1 color clocks (esperado 512 x 443 = 226 816) |
| Diferencia acumulada | −0,185 ms en 22 s |
| Fin del audio contra fin del video | 0,04 ms |
| Reproduccion | 1100 VBL para 1100; 1 frame tarde por 2 VBL, el que predice el encoder |
| Llenar un buffer fib4 | 4,51 ms de media (32 015 ciclos), 4,63 el peor: **7,1 % de la CPU** |

**Sin deriva perceptible.** −0,185 ms en 22 s son 1,5 muestras en total, no
por buffer. Encaja con que Paula pida la primera interrupcion un periodo de
muestra mas tarde que las siguientes (un corrimiento fijo). Con solo la
primera y la ultima estampa no se lo puede separar de una deriva lenta, pero
aun si lo fuera serian 0,5 ms por minuto, cien veces menos de lo que se
nota.

**Bug encontrado midiendo.** La primera corrida daba −27 ms de "deriva": 346
interrupciones en vez de 345, la ultima justo al terminar. El reproductor
cortaba el DMA de audio antes de deshabilitar la interrupcion, y al cortar el
DMA Paula pide una interrupcion mas. Ahora se deshabilita primero y se acusa
la pendiente.

**El modelo de costo, con audio.** Con el audio la decodificacion media paso
de 34,9 a 37,6 ms y el peor delta de 60,3 a 65,2 ms: cada llenado le roba
tiempo al delta que este en curso. El encoder ahora lo simula: los llenados
llegan cada 512 x periodo color clocks desde el VBL del frame 0, cada uno
cuesta `A5_CYC_AUDIO_FILL` = 32 300 ciclos (32 015 medidos dentro de la
rutina, mas ~300 de entrar y salir de la interrupcion, contados a mano), y
un delta que empieza con un llenado en curso espera a que termine.

Validacion contra los 253 deltas medidos, reconstruyendo la linea de tiempo
del reproductor con los tiempos de la Amiga:

- el modelo predice en promedio el **99,9 %** de lo medido (sin el audio,
  93,7 %);
- la reconstruccion da 1 frame tarde, lo mismo que conto la Amiga;
- descontando los llenados, el ajuste da `4202 + 885,5 x filas + 176,5 x
  columnas` (R2 = 0,9981). Para un frame completo difiere del Hito 4 en un
  0,01 %: se mantienen las constantes del Hito 4, medidas sin el ruido del
  audio;
- el residuo maximo, 4,4 ms, es un caso de borde: el delta del frame 26
  empieza justo cuando llega el primer llenado, 0,12 ms despues del VBL, y
  no se puede saber si la estampa quedo antes o despues. Ahi el modelo
  sobreestima: se equivoca del lado seguro.

Con el costo del audio en la linea de tiempo, el encoder produce **el mismo
stream, byte por byte**, que el que se midio: no hubo que degradar nada. La
medicion valida al encoder tal como queda.

**pcm8, medido igual** (el opening con `--audio-format pcm8 --audio-gain
3.3`): llenar un buffer cuesta 3,94 ms (27 918 ciclos, el peor 4,05 ms), el
**6,2 %** de la CPU; `A5_CYC_AUDIO_FILL_PCM8` = 28 200. Sincronia identica a
fib4 (345 buffers, −0,186 ms). La Amiga mostro 4 frames tarde, por 2 VBL el
peor, y la reconstruccion da los mismos 4; con la constante, el encoder
predice esos mismos 4 y genera el mismo stream que se midio, byte por byte
(el modelo da el 99,9 % de lo medido). pcm8 casi no ahorra CPU porque el
bucle es de a una muestra; copiar de a palabras lo bajaria a una decima.

**Segundo bug encontrado midiendo.** La primera medicion de pcm8 dio "el
peor llenado: 1 210 893 ms". La estampa de salida del llenado se tomaba con
la prioridad ya bajada a 2, y el VBL podia entrar entre la lectura del
contador de VBL y la de la posicion del haz: estampa un frame atrasada, resta
negativa. `stamp_irq` corrige un VBL pendiente, no uno que la interrumpe a
mitad. Ahora se vuelve a prioridad 4 antes de estampar, y el decoder avisa si
un llenado mide mas de un frame. Solo afecta al disco de medicion. La media
de fib4 es limpia (su peor llenado, 4,63 ms, es posible); la de pcm8 se
repitio.

**7 % de CPU es bastante.** El bucle fib4 hace por byte dos sumas con tabla,
un corrimiento y una mascara. Con dos tablas de 256 bytes (el paso del nibble
alto y el del bajo, indexadas por el byte entero) bajaria casi a la mitad.
Hoy no hace falta, porque no cambia el atraso previsto; queda anotado.

---

## 2026-09-11 — Hito 6: paleta por franjas, medida

**Que es.** El area activa se parte en franjas horizontales de N filas
logicas, y cada una tiene su paleta de 8 colores por escena. El negro (color
0) es comun a todas, porque tambien es el color del borde. El Copper cambia
los colores 1–7 al principio de la primera linea de cada franja. Formato v4
(`FORMAT.md`).

**Por que entra en el Copper.** Cada linea de pantalla ya tiene un WAIT en
hpos $06 y dos MOVE de modulo (el doblado vertical del Hito 3). En la linea
donde empieza una franja van 7 MOVE de color entre el WAIT y los de modulo:
9 MOVE son 36 color clocks, terminan hacia el color clock 44, y la imagen
empieza en el 64. Con 16 colores serian 17 MOVE, ~74 color clocks, y los
ultimos colores cambiarian con la linea ya empezada: por eso el formato no
admite franjas con 4 planos.

**Medido** con el opening de 22 s y el presupuesto del disco. Hace falta
una metrica nueva, el error de lo que se ve **contra la fuente**: el error
contra el frame cuantizado ideal no sirve para comparar paletas distintas,
porque el ideal cambia con la paleta.

| franjas de | bytes a calidad 8 | umbral final | pixeles a > 0,10 del original | error medio |
|---|---|---|---|---|
| (una paleta) | 862 380 | 0,012 | 12,48 % | 0,0584 |
| 48 filas | 896 388 | 0,045 | 11,14 % | 0,0564 |
| 32 | 892 132 | 0,036 | 10,22 % | 0,0546 |
| 24 | 904 712 | 0,069 | 10,11 % | 0,0541 |
| **16** | 889 744 | 0,012 | **9,62 %** | 0,0535 |
| 12 | 912 110 | 0,074 | 9,95 % | 0,0541 |
| 8 | 889 906 | 0,018 | 9,64 % | 0,0528 |
| 4 | 913 468 | 0,074 | 10,04 % | 0,0538 |
| 1 | 921 744 | 0,078 | 10,19 % | 0,0540 |

Casi toda la ganancia llega con franjas de 16. Mas finas no mejoran: cuestan
mas bytes, y el control de tasa lo paga con perdida. Con 16 filas son 6
franjas, 96 bytes de paleta por corte de escena y 35 MOVE mas de Copper.

**Costuras.** A la vista, las franjas recuperan colores que con una paleta
se perdian (el pelo castano y el vestido de Winry, que con una sola paleta
salen grises). Pero dejan rayas horizontales en las zonas lisas: el fondo
rojo del titulo cae en un tono en una franja y en otro casi igual en la de
abajo. Juntar histogramas de franjas vecinas (`--band-overlap`) no las
quita. Lo que funciona es `--band-snap`: un color a menos de δ de uno de la
franja de arriba pasa a ser exactamente ese.

| δ | pixeles a > 0,10 | bytes | a la vista |
|---|---|---|---|
| 0 | 9,62 % | 889 744 | rayas en el fondo del titulo |
| **0,04** | 10,04 % | 889 576 | la raya grande desaparece |
| 0,06 | 11,17 % | 873 456 | liso, pero la cara pierde color |
| 0,08 | 11,81 % | 874 544 | vuelve al gris de una paleta |

**Decision (confirmada por Az el 2026-09-11,** despues de mirar una paleta y
franjas lado a lado, costuras incluidas**):** franjas de 16 filas con
δ = 0,04 por defecto. Conserva casi
toda la mejora (de 12,5 % a 10,0 % de pixeles lejos del original) y entra en
el disco practicamente sin perdida: la tabla de franjas agrando el
reproductor un sector, y el control de tasa subio el umbral de 0,012 a
0,0128, sin cambio medible en el error ni en el salpicado. `--band-rows 0`
vuelve a la paleta unica, y el stream sale byte por byte igual al del
Hito 5, salvo el numero de version.

**En la Amiga** (disco de medicion, WinUAE A500 cycle-exact, KS 1.2): carga
de 888 954 bytes en 48,3 s; 1100 VBL para 1100; 1 frame tarde por 2 VBL, el
mismo que predice el encoder; el modelo de costo predice el 99,9 % de lo
medido y su ajuste sin el audio (`4435 + 878,1 x filas + 177,3 x columnas`)
es el mismo del Hito 5. Las franjas no le cambian el costo al reproductor:
las paletas se escriben solo en los cortes de escena (~50 palabras) y los 35
MOVE de Copper por frame no se notan.

**Verificacion.** El decoder arma, con la misma receta que `build_copper` +
`write_palette`, el copper list que tendria que tener el reproductor, y lo
compara byte por byte con el que vuelca el disco de prueba
(`build.ps1 still`): 3312 bytes, 6 franjas, identico. La captura de WinUAE
muestra los cambios de paleta sin cortes a mitad de linea.

---

## 2026-09-11 — Dither ordenado: medido, queda apagado

`--dither bayer2|bayer4` existia desde el Hito 1; ahora se midio con el
bitstream real. Cuesta ~5 KB (868 412 y 867 224 bytes contra 862 380) y el
error contra la fuente no mejora (0,0588 contra 0,0584). A la vista, con la
fuerza por defecto casi no se distingue. Queda opcional y apagado, como
pedia `CLAUDE.md`.

---

## 2026-09-11 — Control de tasa: ahora tambien baja la perdida

Si el stream entra y sobra disco, el encoder baja la perdida (busqueda
binaria entre 0 y la calidad pedida) para usarlo. Con el opening no cambia
nada: el umbral de `--quality 8` (0,012) ya es sin perdida, porque dos
colores RGB444 distintos estan siempre mas lejos que eso, y con umbral 0 sale
el mismo stream. Si alguna vez sobra disco sin perdida, lo que queda para
gastar es la histeresis del cuantizador (`--stability`), que es la que mas
bytes ahorra; queda anotado.

---

## 2026-09-11 — Dos bugs de este hito

- `frame_crc` del decoder serializaba las paletas en un buffer de 32 bytes
  (una paleta de 16 colores). Con franjas se desbordaba y los CRC no
  coincidian. La verificacion lo detecto en los tres streams de prueba con
  franjas; los de una paleta pasaban.
- El disco de prueba volcaba el copper list con un segundo `DoIO` y perdia
  el error del primero, porque `DoIO` no preserva d1. Solo afectaba al
  informe.

---

## 2026-09-11 — Hito 7: funciona en la A500 real

Az grabo `work\a500vp.adf` (el opening de 22 s, con franjas de paleta y
audio fib4) en un disquete y lo probo en su A500: segun Az, "funciono a la
perfeccion". No reporto diferencias con WinUAE, asi que no hubo nada que
corregir. No hay mediciones propias del hardware real (tiempo de carga,
sincronia); si alguna vez hacen falta, el disco de medicion (`build.ps1
play`) graba sus resultados en el mismo disquete y se leen desde el PC.

Antes de entregarlo, el disco final (no el de medicion) se arranco entero en
WinUAE con `tools\shot.ps1 -Every 5`: carga al mismo ritmo que el de
medicion y reproduce desde los ~55 s de encendida la maquina.

---

# A5MU: el disco de musica (fork, 2026-09-11)

Lo de arriba es la historia de A500VP. Desde aca, el fork: un disquete con
una cancion y una tapa. Especificacion y decisiones de Az en `CLAUDE.md`.

## 2026-09-11 — M1: ADPCM de 4 bits, medido

**Codec:** IMA ADPCM de 4 bits, un solo flujo sin bloques, estado inicial
0/0, y lo que suena es el byte alto del predictor (`FORMAT.md`). El encoder
elige, para cada muestra, el nibble que minimiza el error de esa muestra
mas el de la siguiente con su mejor nibble (`--lookahead 1`).

**Medido** con el audio completo del opening de FMA (1:32,65, periodo 443,
ganancia automatica al pico a −1 dB = 2,84), SNR de lo que suena contra la
entrada:

| | SNR | bytes |
|---|---|---|
| ADPCM, lookahead 1 | **21,7 dB** | 370 892 (3,9 KB/s) |
| ADPCM, codificador simple | 20,4 dB | igual |
| ADPCM lookahead 1, salida redondeada | 22,1 dB | igual |
| fib4 de A500VP, a la misma ganancia | 12,8 dB | igual |
| pcm8 | 37,8 dB | el doble |

**Lectura honesta:** contra fib4 a la misma ganancia la ventaja es enorme,
pero con el opening, fib4 a su ganancia optima (1,0) daba 20,9 dB. O sea, a
igual distorsion relativa, lo que gana el ADPCM es poder sonar al volumen
maximo (~9 dB mas fuerte), no mucha menos distorsion. El techo es el propio
IMA de 4 bits con musica densa a 8 kHz.

**Salida truncada, no redondeada:** redondear el byte de salida da solo
+0,4 dB; no justifica cambiar el formato ni el decoder del 68000
(`asr.w #8`). Mas adelante, si hace falta calidad al mismo tamano: un
predictor de segundo orden o una busqueda mas profunda en el encoder.

**Duracion:** a 4003 bytes/s, en un disco con tapa HAM6 entran ~3:25.

El decoder de referencia (`a5mu-dec`) decodifica el flujo y coincide con lo
que simulo el encoder (CRC de las 741 784 muestras).

---

## 2026-09-11 — M2: la tapa en HAM6, medida

**Decision:** la tapa es HAM6 de 320x256 (6 planos, 61 440 bytes) sobre una
paleta base de 16 colores sacada por k-means en Oklab, con el color 0 negro
(es tambien el borde). Cada linea arranca desde el color 0.

**Como se elige cada pixel.** HAM deja "flecos": para llegar a un color
lejano hay que cambiar rojo, verde y azul de a uno, y un codificador
codicioso decide sin mirar adelante y se queda con la componente equivocada
para el pixel siguiente. En su lugar hay una **busqueda en haz por linea**:
en cada pixel se guardan los `beam` caminos mas baratos (error Oklab al
cuadrado acumulado), **uno por color resultante distinto** —lo que viene
despues depende solo de ese color, no de como se llego— y al final de la
linea se sigue el mejor hacia atras.

**Medido** con la tapa que dio Az (2560x1440; despues de angostar 15/16 por
el pixel PAL entra como 320x192 con letterbox). Error Oklab medio contra la
imagen fuente y porcentaje de pixeles a mas de 0,1:

| `--ham-beam` | error medio | pixeles a > 0,1 | tiempo |
|---|---|---|---|
| 1 (codicioso) | 0,0154 | 0,07 % | 0,3 s |
| 4 | 0,0148 | 0,06 % | 0,4 s |
| **16 (por defecto)** | **0,0148** | **0,04 %** | **0,7 s** |
| 64 | 0,0148 | 0,03 % | 2,3 s |
| 256 | 0,0148 | 0,03 % | 6,2 s |
| sin HAM, solo los 16 colores | 0,0204 | 0,47 % | — |

**Conclusion:** HAM baja los pixeles malos de 0,47 % a 0,04 %, y eso es
exactamente lo que se ve: los degrades (el fondo, la piel) salen sin
banding. El haz casi no mueve el promedio y algo si la cola; de 16 para
arriba no cambia nada medible, asi que queda 16 por defecto. La tapa cuesta
61 472 bytes fijos en el disco.

**Toggle de audio (`--audio-format adpcm|pcm8`),** como pidio Az: los dos
formatos estan en el encoder y en el decoder de referencia. Con la tapa
adentro, en un disquete entran:

| formato | KB/s | duracion que entra | SNR |
|---|---|---|---|
| adpcm | 3,91 | **3:25** | 21,7 dB |
| pcm8 | 7,82 | **1:42** | 37,8 dB |

Es una decision de Az segun la cancion, no tecnica. Falta que elija cual
quiere para su disco.

**Verificacion:** `a5mu-dec` decodifica los dos archivos (adpcm y pcm8) y
coincide exactamente con el encoder en los dos CRC: el de la tapa —el color
`$0RGB` de los 81 920 pixeles, o sea lo que muestra Denise, no los planos— y
el de las 741 784 muestras de audio.

---

## 2026-09-11 — M3: el disco, verificado en WinUAE

**Lo que hace el reproductor** (`player/music.s`, 2340 bytes; el de medicion
3148): carga la tapa, que es lo primero del disco, y la muestra apenas esta
completa; sigue cargando el audio con una barra de progreso; apaga el motor,
toma el hardware y toca la cancion entera desde RAM. Al terminar calla a
Paula y deja la tapa en pantalla.

**Sin paquetes.** En A500VP el audio viaja dentro de los paquetes de video y
el lector los recorre. Aca el audio es **un solo chorro de bytes**, asi que
el corte entre los dos bloques de RAM cae en cualquier lado y el lector lo
cruza solo (`src_check`). La carga es una maquinita de destinos: paleta,
planos, bloque 1, bloque 2.

**La pantalla mientras carga.** El copper list se arma entero de entrada
pero con `BPLCON0` sin planos y la paleta en negro: la pantalla queda negra
aunque los bitplanes se vayan llenando. Cuando la tapa termina de cargar,
`show_cover` escribe la paleta y enciende los 6 planos (HAM, `$6A00`). Asi
no se ve el dibujo armandose plano por plano, que serian colores basura.

**La barra de progreso es del Copper, no de la CPU.** Va en 8 lineas de la
banda negra de arriba (filas 20..27 = lineas `$40..$47`; tiene que quedar
antes de la linea `$100`, que es hasta donde compara el Copper). Por linea:
un WAIT, `COLOR00` = blanco, otro WAIT que se mueve con la carga, y
`COLOR00` = negro. Funciona porque en la banda negra todos los pixeles son
codigo 0, o sea que muestran `COLOR00` en vivo. El WAIT cuenta de a 2 color
clocks: 76 pasos entre DDFSTRT y DDFSTOP.

**Bug encontrado mirando las capturas.** Para esconder la barra al final
ponia los dos WAIT en la misma posicion, esperando ancho cero. Queda un
muñon de ~50 pixeles: el Copper tarda unos ciclos en ejecutar el segundo
WAIT y su MOVE, y en ese rato el blanco ya esta puesto. Se esconde
pintandola de negro (`hide_bar`), que no depende de ningun tiempo.

**Medido en la Amiga** (WinUAE A500 cycle-exact, KS 1.2, disquetera al
100 %), con el opening de FMA en pcm8 y la tapa de Az, 803 288 bytes:

| | |
|---|---|
| Bloque 1 (slow RAM, `$00C04A18`) | 499 176 bytes |
| Bloque 2 (Chip, `$00018900`) | 242 608 bytes |
| Buffers que pidio Paula | 1450 de 512 muestras |
| Muestras sin sumar al CRC | 0 |
| CRC de lo que sono | **C1140E73, identico al del encoder** |

Las 741 784 muestras que la Amiga le dio a Paula son **byte por byte** las
que simulo el encoder. La carga termino entre los 40 y los 60 s de las
capturas, coherente con los 17,9 KB/s medidos en el Hito 4 de A500VP (no se
cronometro aparte: el reproductor de musica no lleva el TOD).

**Como se verifica.** El reproductor de medicion (`-DBENCH=1`) suma el CRC32
—el mismo de `a5_crc32`— de las muestras **reales**, sin el silencio con que
se rellena el ultimo buffer, y lo graba en el sector 1759. `a5mu-dec
--measure` lo lee del `.adf` y lo compara. El disco de verdad reserva ese
sector y la diferencia de tamano entre los dos reproductores, para que los
mismos datos entren en los dos.

**Lo que no esta medido:** el tiempo de carga con cronometro propio, y el
sonido en si (se verifico el CRC de las muestras, que es mas fuerte que
escuchar, pero no reemplaza a Az escuchandolo en la maquina real).

---

## 2026-09-27 — M4: funciona en la A500 real

**Resultado:** Az grabo el disco y lo probo en su A500: anda. Es posterior a
los reintentos de lectura (`cdc1759`), que se agregaron despues de ver
magenta en un intento anterior en la maquina real.

**Metodo:** prueba de Az en su maquina, reportada de palabra. No se tomaron
cifras (tiempo de carga, disquete, programa de grabado); si aparece una
diferencia con WinUAE, anotarla aca.

---

## 2026-09-10 — Pendiente de medir

- ~~Velocidad de lectura de trackdisk.~~ Medida en el Hito 4: 17,9 KB/s.
- ~~Costo de decodificacion por frame.~~ Medido y calibrado en el Hito 4.
- ~~WAIT horizontal del Copper para el doblado vertical.~~ Resuelto en el
  Hito 3 con el truco del modulo (ver arriba).
- ~~Sincronia de audio y costo del audio en la CPU.~~ Medidos en el Hito 5.
