# A5MU

**English** · [Español](#espanol)

**A song and its cover art on an Amiga 500, from a single floppy disk.**

A5MU turns a song and an image into a bootable DD floppy. The Amiga shows the
cover in HAM6 as soon as it has loaded, loads the rest of the song with a
progress bar, then plays it all from RAM. No AmigaDOS or Workbench needed.

It is a fork of [A500VP](https://github.com/azha2005/amivideo), the
single-floppy video player, and reuses its bootblock, loader and audio code.

```
song + cover ──► ffmpeg ──► a5mu-enc ──► musica.adf ──► A500 / WinUAE
                                │
                                └──► a5mu-dec ──► WAV + image + verification
```

- **Cover in HAM6:** 320×256, 4096 colours, from a 16-colour base palette
  (k-means in Oklab) and a per-line beam search that avoids HAM fringing.
- **Audio:** 4-bit IMA ADPCM or pcm8, mono, at Paula's exact rate
  (period 443 = 8006.5 Hz).
- **Fits on one disk** (with the cover):

  | format | KB/s | length | SNR |
  |---|---|---|---|
  | adpcm | 3.91 | 3:25 | 21.7 dB |
  | pcm8 | 7.82 | 1:42 | 37.8 dB |

- **Verified bit for bit:** in cycle-exact WinUAE, the samples the Amiga fed
  to Paula match the encoder's simulation by CRC.
- **Tested on a real A500.**

**Status:** works on a real A500 and in WinUAE (KS 1.2). The player retries
failed reads and, if a read still fails, writes a diagnostic to the disk.

**Hardware:** A500 PAL, OCS, 68000 at 7 MHz, 512 KB Chip + 512 KB slow (A501),
Kickstart 1.2, DF0.

---

## Requirements and building

The same as A500VP: Windows, PowerShell, gcc from
[MSYS2](https://www.msys2.org/) UCRT64,
[vasm](http://sun.hasenbraten.de/vasm/) (`tools\get-vasm.ps1`), FFmpeg on the
PATH, and optionally [WinUAE](https://www.winuae.net/). Details in
[`docs/SETUP.md`](docs/SETUP.md). The repo neither includes nor downloads
Kickstart ROMs.

```powershell
.\build.ps1        # or: .\build.ps1 -Gcc '...\gcc.exe' -Vasm '...\vasmm68k_mot.exe'
```

Leaves `a5mu-enc.exe`, `a5mu-dec.exe`, `boot.bin` and `music.bin` in `work\`.

## Usage

```powershell
.\work\a5mu-enc.exe --audio 'C:\Music\song.flac' --cover 'C:\Images\cover.jpg' `
  --audio-format adpcm --adf work\musica.adf --wav work\musica.wav --ppm work\cover.ppm
.\work\a5mu-dec.exe --in work\musica.a5m
```

The decoder must confirm that the cover and audio CRCs match. `--wav` and
`--ppm` show exactly how it will sound and look.

**Shortcut:** `.\build.ps1 music -Audio '...' -Cover '...' [-AudioFormat adpcm]`
builds, encodes, verifies and boots the disk in WinUAE, taking screenshots.
It uses `kick12.rom` from the repo root or `-Rom <path>`.

| option | what it does |
|---|---|
| `--audio-format F` | `adpcm` (default) or `pcm8`: better sound, half the length |
| `--start`, `--duration` | which part of the song (0 = all) |
| `--gain F\|auto` | gain; `auto` puts the peak at −1 dB |
| `--cover-fit M` | `letterbox`, `crop`, `stretch` |
| `--ham-beam N` | HAM search paths per line (16; more changes nothing measurable) |
| `--period N` | Paula period (443) |
| `--compare` | also measures the other audio encodings |

Full list: `.\work\a5mu-enc.exe --help`. Format:
[`docs/FORMAT.md`](docs/FORMAT.md) (docs are in Spanish).

## Limitations

- PAL only; one song per disk.
- When the song ends, the cover stays on screen.
- The repo still carries A500VP's video code, used as a base.

Made by Az.

---

<a id="espanol"></a>

# A5MU (español)

[English](#a5mu) · **Español**

**Una canción y su tapa en una Amiga 500, desde un solo disquete.**

A5MU convierte una canción y una imagen en un disquete DD booteable. La Amiga
muestra la tapa en HAM6 apenas termina de cargarla, carga el resto de la
canción con una barra de progreso y después la toca entera desde RAM. No
necesita AmigaDOS ni Workbench.

Es un fork de [A500VP](https://github.com/azha2005/amivideo), el reproductor
de video en un disquete, y reusa su bootblock, su cargador y su audio.

```
canción + tapa ──► ffmpeg ──► a5mu-enc ──► musica.adf ──► A500 / WinUAE
                                  │
                                  └──► a5mu-dec ──► WAV + imagen + verificación
```

- **Tapa en HAM6:** 320×256, 4096 colores, con una paleta base de 16 (k-means
  en Oklab) y una búsqueda en haz por línea que evita los flecos de HAM.
- **Audio:** IMA ADPCM de 4 bits o pcm8, mono, a la frecuencia exacta de Paula
  (período 443 = 8006,5 Hz).
- **Qué entra en un disco** (con la tapa):

  | formato | KB/s | duración | SNR |
  |---|---|---|---|
  | adpcm | 3,91 | 3:25 | 21,7 dB |
  | pcm8 | 7,82 | 1:42 | 37,8 dB |

- **Verificado bit a bit:** en WinUAE cycle-exact, las muestras que la Amiga
  le dio a Paula coinciden por CRC con las que simuló el encoder.
- **Probado en una A500 real.**

**Estado:** funciona en una A500 real y en WinUAE (KS 1.2). El reproductor
reintenta las lecturas fallidas y, si una igual falla, deja un diagnóstico en
el disco.

**Hardware:** A500 PAL, OCS, 68000 a 7 MHz, 512 KB Chip + 512 KB slow (A501),
Kickstart 1.2, DF0.

---

## Requisitos y compilación

Los mismos que A500VP: Windows, PowerShell, gcc de
[MSYS2](https://www.msys2.org/) UCRT64,
[vasm](http://sun.hasenbraten.de/vasm/) (`tools\get-vasm.ps1`), FFmpeg en el
PATH y, opcional, [WinUAE](https://www.winuae.net/). Detalles en
[`docs/SETUP.md`](docs/SETUP.md). El repo no incluye ni descarga ROMs de
Kickstart.

```powershell
.\build.ps1        # o: .\build.ps1 -Gcc '...\gcc.exe' -Vasm '...\vasmm68k_mot.exe'
```

Deja `a5mu-enc.exe`, `a5mu-dec.exe`, `boot.bin` y `music.bin` en `work\`.

## Uso

```powershell
.\work\a5mu-enc.exe --audio 'C:\Musica\cancion.flac' --cover 'C:\Imagenes\tapa.jpg' `
  --audio-format adpcm --adf work\musica.adf --wav work\musica.wav --ppm work\tapa.ppm
.\work\a5mu-dec.exe --in work\musica.a5m
```

El decoder tiene que confirmar que coinciden los CRC de la tapa y del audio.
`--wav` y `--ppm` muestran exactamente cómo va a sonar y verse.

**Atajo:** `.\build.ps1 music -Audio '...' -Cover '...' [-AudioFormat adpcm]`
compila, codifica, verifica y bootea el disco en WinUAE sacando capturas. Usa
`kick12.rom` de la raíz del repo o `-Rom <ruta>`.

| opción | qué hace |
|---|---|
| `--audio-format F` | `adpcm` (por defecto) o `pcm8`: suena mejor y dura la mitad |
| `--start`, `--duration` | qué parte de la canción (0 = toda) |
| `--gain F\|auto` | ganancia; `auto` lleva el pico a −1 dB |
| `--cover-fit M` | `letterbox`, `crop`, `stretch` |
| `--ham-beam N` | caminos por línea en la búsqueda HAM (16; más no cambia nada medible) |
| `--period N` | período de Paula (443) |
| `--compare` | mide también las otras codificaciones del audio |

Lista completa: `.\work\a5mu-enc.exe --help`. Formato:
[`docs/FORMAT.md`](docs/FORMAT.md).

## Limitaciones

- Solo PAL; una canción por disco.
- Al terminar la canción, la tapa queda en pantalla.
- El repo todavía lleva el código de video de A500VP, que usa como base.

Hecho por Az.
