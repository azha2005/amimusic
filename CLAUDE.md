# A5MU: discos de musica para Amiga 500

## Contexto

Este proyecto es de Az. Es un fork de **A500VP** (`C:\Users\JC\Downloads\amivideo`),
el reproductor de video desde un disquete, que funciono en la A500 real de
Az. Aca la idea es otra: un **"disco de musica"**. Un disquete con una
cancion y una imagen fija (la "tapa"), que bootea, muestra la tapa y toca la
cancion.

Hablale a Az en espanol. Los identificadores y comentarios del codigo pueden
ir en ingles.

## Decisiones de Az (2026-09-11; no reabrir sin preguntar)

- **Audio: ADPCM de 4 bits a ~8 kHz, mono.** Entra una cancion entera
  (~3 min 20 s en lo que deja la tapa). Paula a periodo 443 = 8006,535 Hz.
  Sale igual por los dos canales, como en A500VP.
- **Cargar todo y despues sonar.** Nada de leer el disco durante la
  reproduccion.
- **Tapa en HAM6** (4096 colores, 320x256 lowres PAL). Se muestra apenas
  termina de cargar ella misma (los primeros ~60 KB del disco), mientras
  sigue cargando el audio.

## Hardware objetivo (el mismo de A500VP)

- Amiga 500 PAL, OCS, 68000 a ~7 MHz, **Kickstart 1.2** (la ROM de Az; esta
  en la raiz como `kick12.rom`, fuera de git. Nunca descargues ROMs).
- 512 KB de Chip + 512 KB de slow RAM (A501). La slow no es accesible por
  DMA: bitplanes, Copper y Paula leen solo de Chip.
- Una disquetera DD. Si el diseno necesita mas que esto, esta mal.

## Cifras medidas en A500VP (no volver a derivarlas)

- Memoria libre al arrancar con KS 1.2: 487,5 KB de Chip y 487,5 KB de slow.
  El disco (~889 KB de datos) limita antes que la RAM.
- VBL PAL = 3546895 / (313 x 227) = 49,920409 Hz.
- trackdisk con rebote en Chip: 17,9 KB/s. Cargar el disco entero: ~48 s.
- Paula: frecuencia = 3546895 / periodo, exacta. Dos buffers de 512 muestras
  en Chip, llenados por la interrupcion de nivel 4 con la prioridad bajada a
  2; sin deriva medible (`docs/DECISIONS.md`, Hito 5 de A500VP).
- Al terminar: deshabilitar la interrupcion de audio **antes** de cortar el
  DMA, porque al cortarlo Paula pide una mas.
- El cargador de A500VP pone su propio copper list con el sistema operativo
  vivo (`LoadView(NULL)` + dos `WaitTOF`) mientras lee con trackdisk: asi se
  puede mostrar la tapa durante la carga.

## Entorno (igual que A500VP)

Windows y PowerShell (`build.ps1`), gcc de MSYS2 UCRT64, vasm de vbcc,
ffmpeg en el PATH, WinUAE con `a500vp.uae`. Detalles en `docs/SETUP.md`.
**Capturas de WinUAE solo con PrintWindow** (`tools\shot.ps1`): copiar la
pantalla una vez agarro otra aplicacion de Az.

## Arquitectura

```
cancion + tapa ──► ffmpeg ──► encoder (C) ──► musica.adf ──► A500 / WinUAE
                                  │
                                  └──► decoder de referencia (C) ──► WAV + PNG
```

1. **Encoder** (`encoder\music.c`): remuestrea el audio a la frecuencia
   exacta de Paula, lo codifica en ADPCM (`encoder\adpcm.c`), convierte la
   tapa a HAM6 y arma el `.adf` completo.
2. **Decoder de referencia** (`encoder\musicdec.c`): decodifica bit a bit lo
   mismo que el reproductor; da el WAV y el PNG de como va a sonar y verse,
   y verifica con CRC. Un bug de formato no se depura en ensamblador.
3. **Reproductor (asm 68000):** reusa el bootblock, el cargador en dos
   bloques y el audio por nivel 4 de A500VP.

El formato se define en un unico lugar, `docs/FORMAT.md`; si cambia, cambia
la version.

## Hitos

**M0: fork.** Esta especificacion, el repo y el build andando.

**M1: audio en PC.** Codec ADPCM compartido, encoder y decoder de referencia
del audio, WAV de como va a sonar. Medir la calidad (SNR) contra fib4 y pcm8
con una cancion real.

**M2: tapa en PC.** Conversion a HAM6 (paleta base de 16 colores y, por
pixel, "color de paleta" o "cambiar R, G o B del anterior"), PNG de como se
va a ver, error medido contra la original.

**M3: el disco.** Reproductor: carga la tapa, la muestra, carga el audio
con una barrita de progreso, toca. Verificado en WinUAE contra el decoder
de referencia.

**M4: hardware real.** Az graba el disco y lo prueba en su A500.

## Reglas de trabajo (las de A500VP)

- **Medi, no supongas.** Cada medicion va a `docs/DECISIONS.md` con fecha,
  metodo y resultado.
- Primero el decoder de referencia, despues la Amiga.
- Commits chicos, un tema por commit.
- No subas al repo musica, imagenes, ADFs con contenido de terceros ni ROMs.
- Al cerrar cada hito, explicale a Az que se hizo y por que, sin rodeos.
- El codigo de video de A500VP (encode.c, decode.c, player.s, still.s)
  queda en el repo mientras sirva de base; se borra cuando el reproductor de
  musica ya no lo necesite.
