;----------------------------------------------------------------------
; music.s - reproductor del disco de musica (A5MU).
;
; Carga la tapa (paleta + 6 planos HAM6) y la muestra apenas esta
; completa, sigue cargando el audio con una barrita de progreso, y cuando
; termino toma el hardware y toca la cancion entera desde RAM.
;
; El audio se reparte en dos bloques (slow RAM y Chip). A diferencia de
; A500VP no hay paquetes: el audio es un unico chorro de bytes, asi que el
; corte entre bloques cae en cualquier lado y el lector lo cruza solo.
;
; Entrada (desde boot.s): A6 = ExecBase, A1 = IOStdReq de trackdisk,
; A0 = base propia. Codigo independiente de posicion.
;
; Registros que viven todo el programa:
;   a4 = variables (V_*)          a5 = IOStdReq (durante la carga)
;----------------------------------------------------------------------

        include "exec.i"

CHUNK_SECTORS equ 22                  ; un cilindro por lectura
CHUNK_BYTES   equ CHUNK_SECTORS*512
DISK_BYTES    equ 901120
CHIP_MARGIN   equ 4096                ; Chip que se le deja al sistema
HDR_SIZE      equ 32

; --- la tapa: HAM6 de 320x256, 6 planos de 10240 bytes ---
HAM_PLANES    equ 6
HAM_W         equ 320
HAM_H         equ 256
HAM_ROWBYTES  equ HAM_W/8             ; 40
PLANE_SIZE    equ HAM_ROWBYTES*HAM_H  ; 10240
HAM_BYTES     equ HAM_PLANES*PLANE_SIZE
PAL_BYTES     equ 32                  ; 16 colores

; --- audio ---
AUD_SAMPLES   equ 512                 ; muestras por buffer (64 ms)
AUD_WORDS     equ AUD_SAMPLES/2
AFMT_ADPCM    equ 1
AFMT_PCM8     equ 2

; --- barra de carga ---
; Va en la banda negra de arriba (con letterbox la imagen ocupa las filas
; 32..223). El Copper compara 8 bits de linea, asi que tiene que quedar
; antes de la $100: las filas 20..27 son las lineas $40..$47.
BAR_LINE      equ $40
BAR_LINES     equ 8
BAR_H0        equ $39                 ; WAIT en hpos = color clock 56 (DDFSTRT)
BAR_STEPS     equ 76                  ; hasta el color clock 208 (DDFSTOP)
COL_BAR       equ $0fff

; --- copper list: offsets en bytes desde el principio ---
COP_SETUP     equ 32                  ; 8 MOVE (DIW, DDF, BPLCON1/2, modulos)
COP_BPLPT     equ COP_SETUP           ; 6 planos x 2 MOVE
COP_COLOR     equ COP_BPLPT+HAM_PLANES*8
COP_BPLCON0   equ COP_COLOR+PAL_BYTES*2
COP_BAR       equ COP_BPLCON0+4
COP_END       equ COP_BAR+BAR_LINES*16
COPPER_BYTES  equ COP_END+4

INFO_SECTOR   equ 1759                ; medicion y diagnostico se graban aca
READ_TRIES    equ 5                   ; intentos por lectura

COL_NOMEM     equ $0f00               ; rojo: sin memoria
COL_DISK      equ $0f0f               ; magenta: fallo de trackdisk
COL_BADHDR    equ $0ff0               ; amarillo: datos invalidos

;----------------------------------------------------------------------
; Variables, relativas a a4. La interrupcion de audio tambien las usa.
;----------------------------------------------------------------------
V_IOREQ     equ 0       ; l
V_BOUNCE    equ 4       ; l  rebote en Chip para trackdisk
V_PLANES    equ 8       ; l  los 6 bitplanes de la tapa
V_COP       equ 12      ; l  copper list
V_ABUF0     equ 16      ; l  buffers de Paula (contiguos: V_ABUF0 + i*4)
V_ABUF1     equ 20      ; l
V_BLK1      equ 24      ; l  bloque 1 (slow RAM)
V_BLK1END   equ 28      ; l
V_BLK1SIZE  equ 32      ; l
V_BLK2      equ 36      ; l  bloque 2 (Chip)
V_BLK2END   equ 40      ; l
V_BLK2SIZE  equ 44      ; l
V_APTR      equ 48      ; l  lectura de audio
V_AEND      equ 52      ; l  fin del bloque en el que esta leyendo
V_AREM      equ 56      ; l  bytes de audio que quedan por leer
V_APRED     equ 60      ; l  ADPCM: predictor
V_AIDX      equ 64      ; l  ADPCM: indice, ya multiplicado por 2
V_ACOUNT    equ 68      ; l  interrupciones de audio atendidas
V_ASTOP     equ 72      ; l  en que cuenta cortar (0 = todavia no)
V_ABLK2     equ 76      ; w  el lector ya paso al bloque 2
V_ANEXT     equ 78      ; w  buffer que toca llenar
V_AFMT      equ 80      ; w
V_APER      equ 82      ; w
V_ALASTB    equ 84      ; w  ultima muestra emitida (para el silencio)
V_COVER     equ 86      ; w  hay tapa
V_DI        equ 88      ; w  destino de carga actual
V_DPTR      equ 90      ; l
V_DLEN      equ 94      ; l
V_LOADED    equ 98      ; l  bytes ya repartidos
V_DTOT      equ 102     ; l  bytes a repartir
V_STEP      equ 106     ; l  bytes por paso de la barra
V_OLDINT4   equ 110     ; l
V_OLDINTENA equ 114     ; w
V_OLDDMA    equ 116     ; w
V_PAL       equ 118     ; 32 bytes, la paleta tal cual viene del disco
V_CRC       equ 150     ; l  CRC de lo que sono (ya invertido, como en el C)
V_CRCLEFT   equ 154     ; l  muestras que faltan sumar al CRC
V_NSAMP     equ 158     ; l  muestras de la cancion
V_ERRCODE   equ 162     ; l  ultimo error de trackdisk
V_ERROFF    equ 166     ; l  offset en el disco donde fallo
V_ERRTRY    equ 170     ; l  reintentos gastados en esa lectura
VARS_SIZE   equ 174

;----------------------------------------------------------------------
; Cabecera del reproductor. adf_assemble escribe donde quedaron los datos.
;----------------------------------------------------------------------
player_start:
        bra.w   entry
        dc.b    "A5PL"
hdr_data_off:   dc.l    0             ; offset en bytes desde el inicio del disco
hdr_data_len:   dc.l    0             ; bytes

entry:
        move.l  4.w,a6
        lea     vars(pc),a4
        move.l  a1,V_IOREQ(a4)
        move.l  a1,a5

        ;--- rebote: trackdisk de KS 1.x solo lee a Chip ------------
        move.l  #CHUNK_BYTES,d0
        move.l  #MEMF_CHIP,d1
        jsr     _LVOAllocMem(a6)
        move.l  d0,V_BOUNCE(a4)
        beq     nomem

        ;--- primer pedazo: trae la cabecera ------------------------
        move.l  hdr_data_len(pc),d0
        cmp.l   #HDR_SIZE,d0
        blo     badhdr
        move.l  hdr_data_off(pc),d0
        bsr     read_chunk
        bne     diskerr

        move.l  V_BOUNCE(a4),a0
        lea     header(pc),a1
        moveq   #HDR_SIZE/4-1,d0
.hdr:   move.l  (a0)+,(a1)+
        dbf     d0,.hdr

        lea     header(pc),a0
        cmp.l   #$41354d55,(a0)               ; "A5MU"
        bne     badhdr
        cmp.w   #1,4(a0)                      ; version de formato
        bne     badhdr

        moveq   #0,d0                         ; la tapa
        move.b  8(a0),d0                      ; planos
        beq.s   .nocover
        cmp.w   #HAM_PLANES,d0
        bne     badhdr
        cmp.b   #1,9(a0)                      ; modo: HAM
        bne     badhdr
        cmp.w   #HAM_W,10(a0)
        bne     badhdr
        cmp.w   #HAM_H,12(a0)
        bne     badhdr
        move.w  #1,V_COVER(a4)
.nocover:
        move.w  14(a0),d0                     ; periodo: Paula no baja de 124
        cmp.w   #124,d0
        blo     badhdr
        move.w  d0,V_APER(a4)
        moveq   #0,d0
        move.b  16(a0),d0                     ; formato de audio
        beq     badhdr
        cmp.w   #AFMT_PCM8,d0
        bhi     badhdr
        move.w  d0,V_AFMT(a4)

        move.l  20(a0),V_NSAMP(a4)            ; muestras
        move.l  24(a0),d3                     ; d3 = bytes de audio
        beq     badhdr

        ifd     BENCH
        move.l  V_NSAMP(a4),V_CRCLEFT(a4)
        move.l  #$ffffffff,V_CRC(a4)          ; el C arranca en ~0
        endc

        ; Lo que hay para repartir tiene que dar exacto: tapa + audio.
        move.l  hdr_data_len(pc),d0
        sub.l   #HDR_SIZE,d0
        move.l  d0,V_DTOT(a4)
        sub.l   d3,d0
        tst.w   V_COVER(a4)
        beq.s   .nosz
        sub.l   #PAL_BYTES+HAM_BYTES,d0
.nosz:  tst.l   d0
        bne     badhdr

        ;--- memoria de la tapa y del copper list, en Chip ----------
        tst.w   V_COVER(a4)
        beq.s   .noplanes
        move.l  #HAM_BYTES,d0
        move.l  #MEMF_CHIP|MEMF_CLEAR,d1
        jsr     _LVOAllocMem(a6)
        move.l  d0,V_PLANES(a4)
        beq     nomem
.noplanes:
        move.l  #COPPER_BYTES,d0
        move.l  #MEMF_CHIP|MEMF_CLEAR,d1
        jsr     _LVOAllocMem(a6)
        move.l  d0,V_COP(a4)
        beq     nomem

        ;--- buffers de Paula: solo lee Chip ------------------------
        move.l  #2*AUD_SAMPLES,d0
        move.l  #MEMF_CHIP|MEMF_CLEAR,d1
        jsr     _LVOAllocMem(a6)
        move.l  d0,V_ABUF0(a4)
        beq     nomem
        add.l   #AUD_SAMPLES,d0
        move.l  d0,V_ABUF1(a4)

        ;--- bloques para el audio ---------------------------------
        ; 1: el mayor bloque no-Chip, que en una A500 es la slow RAM.
        move.l  #MEMF_FAST|MEMF_LARGEST,d1
        jsr     _LVOAvailMem(a6)
        cmp.l   d3,d0
        bls.s   .b1size
        move.l  d3,d0
.b1size:
        move.l  d0,V_BLK1SIZE(a4)
        beq.s   .b1none
        move.l  #MEMF_FAST,d1
        jsr     _LVOAllocMem(a6)
        move.l  d0,V_BLK1(a4)
        bne.s   .b1done
.b1none:
        clr.l   V_BLK1(a4)
        clr.l   V_BLK1SIZE(a4)
.b1done:
        move.l  d3,d2                         ; d2 = lo que falta: al bloque 2
        sub.l   V_BLK1SIZE(a4),d2
        beq.s   .b2none
        move.l  #MEMF_CHIP|MEMF_LARGEST,d1
        jsr     _LVOAvailMem(a6)
        sub.l   #CHIP_MARGIN,d0
        bls     nomem
        cmp.l   d2,d0
        blo     nomem
        move.l  d2,d0
        move.l  d0,V_BLK2SIZE(a4)
        move.l  #MEMF_CHIP,d1
        jsr     _LVOAllocMem(a6)
        move.l  d0,V_BLK2(a4)
        beq     nomem
        bra.s   .blocks
.b2none:
        clr.l   V_BLK2(a4)
        clr.l   V_BLK2SIZE(a4)
.blocks:
        move.l  V_BLK1(a4),d0                 ; los finales, para el lector
        add.l   V_BLK1SIZE(a4),d0
        move.l  d0,V_BLK1END(a4)
        move.l  V_BLK2(a4),d0
        add.l   V_BLK2SIZE(a4),d0
        move.l  d0,V_BLK2END(a4)
        move.l  d3,V_AREM(a4)

        ;--- pantalla: copper list armado, pero todavia sin planos --
        bsr     build_copper
        move.l  V_DTOT(a4),d0                 ; bytes por paso de la barra
        divu    #BAR_STEPS,d0
        and.l   #$ffff,d0
        bne.s   .step
        moveq   #1,d0
.step:  move.l  d0,V_STEP(a4)

        lea     gfxname(pc),a1
        moveq   #0,d0
        jsr     _LVOOpenLibrary(a6)
        tst.l   d0
        beq     nomem
        move.l  d0,a6                         ; a6 = GfxBase
        sub.l   a1,a1
        jsr     _LVOLoadView(a6)
        jsr     _LVOWaitTOF(a6)
        jsr     _LVOWaitTOF(a6)
        move.l  4.w,a6
        lea     CUSTOM,a1
        move.l  V_COP(a4),COP1LC(a1)
        move.w  d0,COPJMP1(a1)
        move.w  #$8380,DMACON(a1)             ; MASTER|RASTER|COPPER

        ;--- carga: los bytes van cayendo en los destinos ----------
        ; d4 = offset en disco, d5 = bytes que faltan leer,
        ; a0 = origen en el rebote, d2 = bytes disponibles del pedazo.
        move.w  #-1,V_DI(a4)
        clr.l   V_DLEN(a4)
        move.l  hdr_data_off(pc),d4
        move.l  hdr_data_len(pc),d5
        move.l  V_BOUNCE(a4),a0               ; el primer pedazo ya esta leido
        move.l  #CHUNK_BYTES,d2
        cmp.l   d5,d2
        bls.s   .first
        move.l  d5,d2
.first: sub.l   d2,d5
        lea     HDR_SIZE(a0),a0               ; la cabecera ya se copio
        sub.l   #HDR_SIZE,d2
        bra.s   .copy

.next:  add.l   #CHUNK_BYTES,d4
        move.l  d4,d0
        bsr     read_chunk
        bne     diskerr
        move.l  V_BOUNCE(a4),a0
        move.l  #CHUNK_BYTES,d2
        cmp.l   d5,d2
        bls.s   .take
        move.l  d5,d2
.take:  sub.l   d2,d5

.copy:  tst.l   d2
        beq.s   .chunkdone
        move.l  V_DLEN(a4),d1
        bne.s   .have
        bsr     dest_next                     ; Z = 1: no hay mas destinos
        beq     badhdr
        move.l  V_DLEN(a4),d1
.have:  cmp.l   d2,d1                         ; min(destino, pedazo)
        bls.s   .n
        move.l  d2,d1
.n:     sub.l   d1,V_DLEN(a4)
        sub.l   d1,d2
        add.l   d1,V_LOADED(a4)
        move.l  V_DPTR(a4),a2
        move.l  d1,d3
        subq.l  #1,d3                         ; d1 <= CHUNK_BYTES: entra en dbf
.cb:    move.b  (a0)+,(a2)+
        dbf     d3,.cb
        move.l  a2,V_DPTR(a4)
        bra.s   .copy

.chunkdone:
        bsr     progress
        tst.l   d5
        bne     .next

        tst.l   V_DLEN(a4)                    ; quedo un destino sin llenar
        bne     badhdr
        bsr     show_cover                    ; por si no hubo audio despues

        move.l  a5,a1                         ; apagar el motor
        move.w  #TD_MOTOR,IO_COMMAND(a1)
        clr.l   IO_LENGTH(a1)
        jsr     _LVODoIO(a6)

        ;--- el lector de audio arranca al principio del bloque 1 ---
        move.l  V_BLK1(a4),d0
        move.l  V_BLK1END(a4),d1
        tst.l   V_BLK1SIZE(a4)
        bne.s   .from1
        move.w  #1,V_ABLK2(a4)                ; no hubo slow RAM
        move.l  V_BLK2(a4),d0
        move.l  V_BLK2END(a4),d1
.from1: move.l  d0,V_APTR(a4)
        move.l  d1,V_AEND(a4)

        ;--- tomar el hardware -------------------------------------
        jsr     _LVOForbid(a6)
        lea     CUSTOM,a0
        move.w  INTENAR(a0),V_OLDINTENA(a4)
        move.w  DMACONR(a0),V_OLDDMA(a4)
        move.w  #$7fff,INTENA(a0)
        move.w  #$7fff,INTREQ(a0)
        move.w  #$7fff,DMACON(a0)
        move.l  $70.w,V_OLDINT4(a4)
        lea     level4(pc),a1
        move.l  a1,$70.w

        move.l  V_ABUF0(a4),a0                ; el primer buffer, lleno
        bsr     audio_fill
        ifd     BENCH
        bsr     crc_buffer
        endc
        move.w  #1,V_ANEXT(a4)

        lea     CUSTOM,a0
        move.l  V_ABUF0(a4),d0
        move.l  d0,AUD0LC(a0)                 ; los dos canales, mismo buffer
        move.l  d0,AUD1LC(a0)
        move.w  #AUD_WORDS,AUD0LEN(a0)
        move.w  #AUD_WORDS,AUD1LEN(a0)
        move.w  V_APER(a4),AUD0PER(a0)
        move.w  V_APER(a4),AUD1PER(a0)
        move.w  #64,AUD0VOL(a0)
        move.w  #64,AUD1VOL(a0)
        bclr    #1,CIAA_PRA                   ; filtro pasabajos encendido

        move.l  V_COP(a4),COP1LC(a0)
        move.w  d0,COPJMP1(a0)
        move.w  #$8380,DMACON(a0)             ; MASTER|RASTER|COPPER
        move.w  #$c080,INTENA(a0)             ; INTEN|AUD0
        move.w  #$8203,DMACON(a0)             ; AUD0EN|AUD1EN

        bsr     hide_bar

        ;--- sonando: no hay nada que hacer hasta que se acabe ------
.play:  move.l  V_ASTOP(a4),d0
        beq.s   .play
        cmp.l   V_ACOUNT(a4),d0
        bhi.s   .play

        ; Callar a Paula: primero la interrupcion y despues el DMA, porque
        ; al cortar el DMA Paula pide una interrupcion mas (A500VP, Hito 5).
        lea     CUSTOM,a0
        move.w  #$0080,INTENA(a0)             ; AUD0
        move.w  #$0003,DMACON(a0)             ; AUD0EN|AUD1EN
        move.w  #$0080,INTREQ(a0)
        move.w  #$0080,INTREQ(a0)

        ifd     BENCH
        bsr     bench_finish
        endc
.forever:
        bra.s   .forever

;----------------------------------------------------------------------
; Errores: el color se reescribe en un lazo para que el copper list del
; sistema, si todavia esta, no lo tape.
;----------------------------------------------------------------------
nomem:  move.w  #COL_NOMEM,d0
        bra.s   stop
diskerr:
        bra     disk_fail
badhdr: move.w  #COL_BADHDR,d0
stop:   lea     CUSTOM,a0
.loop:  move.w  d0,COLOR00(a0)
        bra.s   .loop

;----------------------------------------------------------------------
; dest_next - pasa al destino siguiente de la carga y le fija puntero y
; longitud. Z = 1 si ya no queda ninguno.
;   0 = paleta de la tapa, 1 = planos, 2 = bloque 1, 3 = bloque 2.
;----------------------------------------------------------------------
dest_next:
        move.l  d0,-(sp)
.again: move.w  V_DI(a4),d0
        addq.w  #1,d0
        move.w  d0,V_DI(a4)
        cmp.w   #1,d0
        bhi.s   .d23
        tst.w   V_COVER(a4)                   ; sin tapa: saltear los dos
        beq.s   .again
        tst.w   d0
        bne.s   .d1
        move.l  a4,V_DPTR(a4)                 ; 0: la paleta, dentro de vars
        add.l   #V_PAL,V_DPTR(a4)
        move.l  #PAL_BYTES,V_DLEN(a4)
        bra.s   .ok
.d1:    move.l  V_PLANES(a4),V_DPTR(a4)       ; 1: los planos
        move.l  #HAM_BYTES,V_DLEN(a4)
        bra.s   .ok
.d23:   cmp.w   #2,d0
        bne.s   .d3
        bsr     show_cover                    ; la tapa quedo completa
        move.l  V_BLK1SIZE(a4),d0             ; 2: el bloque 1
        beq.s   .again
        move.l  V_BLK1(a4),V_DPTR(a4)
        move.l  d0,V_DLEN(a4)
        bra.s   .ok
.d3:    cmp.w   #3,d0
        bne.s   .none
        move.l  V_BLK2SIZE(a4),d0             ; 3: el bloque 2
        beq.s   .again
        move.l  V_BLK2(a4),V_DPTR(a4)
        move.l  d0,V_DLEN(a4)
.ok:    move.l  (sp)+,d0                      ; el lazo de carga tiene d1 y a0
        tst.l   V_DLEN(a4)                    ; vivos: aca no se tocan. Z = 0
        rts
.none:  move.l  (sp)+,d0
        clr.l   V_DLEN(a4)
        tst.l   V_DLEN(a4)                    ; Z = 1
        rts

;----------------------------------------------------------------------
; build_copper - arma el copper list de la tapa. Los planos arrancan
; apagados (BPLCON0 sin planos) y la paleta en negro: los enciende
; show_cover cuando la tapa termino de cargar. Abajo de todo, la barra.
;----------------------------------------------------------------------
build_copper:
        movem.l d0-d3/a0-a1,-(sp)
        move.l  V_COP(a4),a0
        move.l  #$008e2c81,(a0)+              ; DIWSTRT
        move.l  #$00902cc1,(a0)+              ; DIWSTOP (256 lineas)
        move.l  #$00920038,(a0)+              ; DDFSTRT
        move.l  #$009400d0,(a0)+              ; DDFSTOP
        move.l  #$01020000,(a0)+              ; BPLCON1
        move.l  #$01040000,(a0)+              ; BPLCON2
        move.l  #$01080000,(a0)+              ; BPL1MOD: la tapa no se dobla
        move.l  #$010a0000,(a0)+              ; BPL2MOD

        move.l  V_PLANES(a4),d0               ; punteros de bitplane
        move.w  #$00e0,d1
        moveq   #HAM_PLANES-1,d2
.bp:    swap    d0
        move.w  d1,(a0)+
        move.w  d0,(a0)+
        addq.w  #2,d1
        swap    d0
        move.w  d1,(a0)+
        move.w  d0,(a0)+
        addq.w  #2,d1
        add.l   #PLANE_SIZE,d0
        dbf     d2,.bp

        move.w  #$0180,d1                     ; COLOR00..15, en negro
        moveq   #15,d2
.pal:   move.w  d1,(a0)+
        clr.w   (a0)+
        addq.w  #2,d1
        dbf     d2,.pal

        move.w  #$0100,(a0)+                  ; BPLCON0: todavia sin planos
        move.w  #$0200,(a0)+

        move.w  #BAR_LINE,d3                  ; la barra
        moveq   #BAR_LINES-1,d2
.bar:   move.w  d3,d0
        lsl.w   #8,d0
        or.w    #BAR_H0,d0
        move.w  d0,(a0)+                      ; WAIT: arranca la barra
        move.w  #$fffe,(a0)+
        move.w  #$0180,(a0)+
        move.w  #COL_BAR,(a0)+
        move.w  d0,(a0)+                      ; WAIT: hasta aca llego
        move.w  #$fffe,(a0)+
        move.w  #$0180,(a0)+
        clr.w   (a0)+
        addq.w  #1,d3
        dbf     d2,.bar

        move.l  #$fffffffe,(a0)+              ; fin
        movem.l (sp)+,d0-d3/a0-a1
        rts

;----------------------------------------------------------------------
; show_cover - pasa la paleta al copper list y enciende los 6 planos. Se
; llama una sola vez, cuando la tapa ya esta entera en Chip.
;----------------------------------------------------------------------
show_cover:
        movem.l d0-d2/a0-a1,-(sp)
        tst.w   V_COVER(a4)
        beq.s   .out
        clr.w   V_COVER(a4)                   ; una sola vez
        move.l  V_COP(a4),a0
        lea     COP_COLOR+2(a0),a1
        lea     V_PAL(a4),a0
        moveq   #15,d2
.c:     move.w  (a0)+,(a1)
        addq.l  #4,a1
        dbf     d2,.c
        move.l  V_COP(a4),a0
        move.w  #$6a00,COP_BPLCON0+2(a0)      ; 6 planos, HAM, COLOR
.out:   movem.l (sp)+,d0-d2/a0-a1
        rts

;----------------------------------------------------------------------
; progress - mueve el fin de la barra segun V_LOADED de V_DTOT.
;----------------------------------------------------------------------
progress:
        movem.l d0-d3/a0,-(sp)
        move.l  V_LOADED(a4),d0
        divu    V_STEP+2(a4),d0               ; el paso entra en una palabra
        and.l   #$ffff,d0
        cmp.w   #BAR_STEPS,d0
        bls.s   .ok
        move.w  #BAR_STEPS,d0
.ok:    add.w   d0,d0                         ; el WAIT cuenta de a 2 CCK
        add.w   #BAR_H0,d0
        move.l  V_COP(a4),a0
        lea     COP_BAR+8(a0),a0              ; el segundo WAIT de cada linea
        move.w  #BAR_LINE,d2
        moveq   #BAR_LINES-1,d1
.l:     move.w  d2,d3
        lsl.w   #8,d3
        or.w    d0,d3
        move.w  d3,(a0)
        lea     16(a0),a0
        addq.w  #1,d2
        dbf     d1,.l
        movem.l (sp)+,d0-d3/a0
        rts

;----------------------------------------------------------------------
; hide_bar - apaga la barra pintandola de negro. No alcanza con poner los
; dos WAIT en la misma posicion: el Copper tarda unos ciclos en ejecutar el
; segundo y queda un muñon de ~50 pixeles (visto en WinUAE).
;----------------------------------------------------------------------
hide_bar:
        movem.l d0/a0,-(sp)
        move.l  V_COP(a4),a0
        lea     COP_BAR+6(a0),a0
        moveq   #BAR_LINES-1,d0
.l:     clr.w   (a0)
        lea     16(a0),a0
        dbf     d0,.l
        movem.l (sp)+,d0/a0
        rts

;----------------------------------------------------------------------
; read_chunk - lee CHUNK_BYTES (o lo que quede de disco) al rebote.
;   d0 = offset en disco, multiplo de 512.  Z = 1 si salio bien.
;
; Reintenta hasta READ_TRIES veces. En hardware real una lectura marginal
; suele salir al segundo intento; apagar el motor entre intentos hace que
; el proximo acceso recalibre la cabeza.
;----------------------------------------------------------------------
read_chunk:
        movem.l d1-d3/a0-a1,-(sp)
        move.l  d0,d3                         ; d3 = offset
        moveq   #READ_TRIES,d2
.try:   move.l  #DISK_BYTES,d1
        sub.l   d3,d1
        cmp.l   #CHUNK_BYTES,d1
        bls.s   .len
        move.l  #CHUNK_BYTES,d1
.len:   move.l  a5,a1
        move.w  #CMD_READ,IO_COMMAND(a1)
        move.l  d1,IO_LENGTH(a1)
        move.l  V_BOUNCE(a4),IO_DATA(a1)
        move.l  d3,IO_OFFSET(a1)
        jsr     _LVODoIO(a6)
        tst.l   d0
        beq.s   .out
        move.l  d0,V_ERRCODE(a4)              ; guardar por si no sale
        move.l  d3,V_ERROFF(a4)
        subq.l  #1,d2
        beq.s   .out
        move.l  a5,a1                         ; motor abajo y de nuevo
        move.w  #TD_MOTOR,IO_COMMAND(a1)
        clr.l   IO_LENGTH(a1)
        jsr     _LVODoIO(a6)
        bra.s   .try
.out:   move.l  #READ_TRIES,d1
        sub.l   d2,d1
        move.l  d1,V_ERRTRY(a4)
        tst.l   d0
        movem.l (sp)+,d1-d3/a0-a1             ; movem no toca los flags
        rts

;----------------------------------------------------------------------
; disk_fail - no se pudo leer. El disquete igual se puede *escribir*, asi
; que deja el diagnostico en el ultimo sector y despues se lee desde el PC
; con a5mu-dec --measure. Si esta protegido contra escritura, no pasa nada:
; queda la pantalla magenta igual.
;----------------------------------------------------------------------
disk_fail:
        move.l  V_BOUNCE(a4),a0
        move.w  #127,d0
.clr:   clr.l   (a0)+
        dbf     d0,.clr
        move.l  V_BOUNCE(a4),a0
        move.l  #$44455252,(a0)+              ; "DERR"
        move.l  V_ERRCODE(a4),(a0)+
        move.l  V_ERROFF(a4),(a0)+
        move.l  V_ERRTRY(a4),(a0)+
        move.l  V_LOADED(a4),(a0)+
        move.l  V_DTOT(a4),(a0)+
        move.w  V_DI(a4),d0                   ; destino en el que estaba
        ext.l   d0
        move.l  d0,(a0)+
        move.l  hdr_data_off(pc),(a0)+
        move.l  hdr_data_len(pc),(a0)+

        move.l  a5,a1
        move.w  #CMD_WRITE,IO_COMMAND(a1)
        move.l  #512,IO_LENGTH(a1)
        move.l  V_BOUNCE(a4),IO_DATA(a1)
        move.l  #INFO_SECTOR*512,IO_OFFSET(a1)
        jsr     _LVODoIO(a6)
        move.l  a5,a1                         ; bajar la pista a disco
        move.w  #CMD_UPDATE,IO_COMMAND(a1)
        clr.l   IO_LENGTH(a1)
        jsr     _LVODoIO(a6)
        move.l  a5,a1
        move.w  #TD_MOTOR,IO_COMMAND(a1)
        clr.l   IO_LENGTH(a1)
        jsr     _LVODoIO(a6)
        move.w  #COL_DISK,d0
        bra     stop

;----------------------------------------------------------------------
; level4 - interrupcion de audio del canal 0. Paula engancho el buffer que
; se le encolo la vez anterior; el otro quedo libre: se lo llena y se lo
; encola.
;----------------------------------------------------------------------
level4:
        movem.l d0-d7/a0-a4,-(sp)
        lea     CUSTOM,a0
        move.w  #$0080,INTREQ(a0)             ; AUD0
        move.w  #$0080,INTREQ(a0)
        lea     vars(pc),a4
        addq.l  #1,V_ACOUNT(a4)
        move.w  #$2200,sr                     ; dejar pasar niveles menores
        move.w  V_ANEXT(a4),d0
        add.w   d0,d0
        add.w   d0,d0
        lea     V_ABUF0(a4),a0
        move.l  0(a0,d0.w),a0
        bsr     audio_fill

        ifd     BENCH
        bsr     crc_buffer                    ; a0 = el buffer recien llenado
        endc

        lea     CUSTOM,a1
        move.l  a0,AUD0LC(a1)                 ; los dos canales, mismo buffer
        move.l  a0,AUD1LC(a1)
        eor.w   #1,V_ANEXT(a4)
        movem.l (sp)+,d0-d7/a0-a4
        rte

;----------------------------------------------------------------------
; audio_fill - llena un buffer de Paula con las proximas AUD_SAMPLES
; muestras. Cuando el flujo se acaba repite la ultima muestra (silencio) y
; anota en V_ASTOP en que cuenta de interrupciones hay que cortar: dos mas,
; para que se termine de oir lo que ya estaba encolado.
;   a0 = buffer, a4 = variables. Preserva todo.
;----------------------------------------------------------------------
audio_fill:
        movem.l d0-d7/a0-a3,-(sp)
        move.w  #AUD_SAMPLES,d5
        move.l  V_APTR(a4),a1
        move.l  V_AREM(a4),d6
        move.l  V_APRED(a4),d2                ; estado ADPCM (pcm8 no lo toca)
        move.l  V_AIDX(a4),d4
        lea     steptab(pc),a2
        lea     idxtab(pc),a3
        cmp.w   #AFMT_PCM8,V_AFMT(a4)
        beq.s   .pcm

.ad:    tst.l   d6                            ; ADPCM: dos muestras por byte
        beq.s   .sil
        bsr     src_check
        moveq   #0,d0
        move.b  (a1)+,d0
        subq.l  #1,d6
        move.w  d0,d7
        lsr.w   #4,d7
        bsr     adpcm_step
        move.b  d3,(a0)+
        move.w  d0,d7
        and.w   #15,d7
        bsr     adpcm_step
        move.b  d3,(a0)+
        subq.w  #2,d5                         ; el buffer tiene muestras pares
        bne.s   .ad
        bra.s   .end

.pcm:   tst.l   d6                            ; pcm8: una muestra por byte
        beq.s   .sil
        bsr     src_check
        move.b  (a1)+,(a0)+
        subq.l  #1,d6
        subq.w  #1,d5
        bne.s   .pcm
        bra.s   .end

.sil:   tst.l   V_ASTOP(a4)                   ; se acabo la cancion
        bne.s   .noset
        move.l  V_ACOUNT(a4),d1
        addq.l  #2,d1
        move.l  d1,V_ASTOP(a4)
.noset: cmp.w   #AUD_SAMPLES,d5               ; la ultima muestra que sono
        beq.s   .silprev
        move.b  -1(a0),d0
        bra.s   .silgo
.silprev:
        move.b  V_ALASTB+1(a4),d0
.silgo: move.b  d0,(a0)+
        subq.w  #1,d5
        bne.s   .silgo
        bra.s   .done

.end:   move.b  -1(a0),d0
        move.b  d0,V_ALASTB+1(a4)
.done:  move.l  a1,V_APTR(a4)
        move.l  d6,V_AREM(a4)
        move.l  d2,V_APRED(a4)
        move.l  d4,V_AIDX(a4)
        movem.l (sp)+,d0-d7/a0-a3
        rts

;----------------------------------------------------------------------
; src_check - si la lectura llego al fin del bloque 1, sigue en el 2.
;   a1 = lectura. No toca ningun otro registro.
;----------------------------------------------------------------------
src_check:
        cmp.l   V_AEND(a4),a1
        bne.s   .ok
        tst.w   V_ABLK2(a4)
        bne.s   .ok
        move.w  #1,V_ABLK2(a4)
        move.l  V_BLK2(a4),a1
        move.l  V_BLK2END(a4),V_AEND(a4)
.ok:    rts

;----------------------------------------------------------------------
; adpcm_step - un nibble de IMA ADPCM (encoder/adpcm.c, bit a bit).
;   entra d7 = nibble, d2 = predictor, d4 = indice x 2,
;         a2 = tabla de pasos, a3 = tabla de indices.
;   sale  d3 = la muestra de 8 bits. Toca d1 y d3.
;----------------------------------------------------------------------
adpcm_step:
        move.w  0(a2,d4.w),d1                 ; step (siempre positivo)
        and.l   #$ffff,d1
        move.l  d1,d3
        lsr.l   #3,d3                         ; diff = step>>3
        btst    #2,d7
        beq.s   .n4
        add.l   d1,d3
.n4:    lsr.l   #1,d1                         ; step>>1
        btst    #1,d7
        beq.s   .n2
        add.l   d1,d3
.n2:    lsr.l   #1,d1                         ; step>>2
        btst    #0,d7
        beq.s   .n1
        add.l   d1,d3
.n1:    btst    #3,d7                         ; el bit 3 es el signo
        beq.s   .plus
        sub.l   d3,d2
        bra.s   .clamp
.plus:  add.l   d3,d2
.clamp: cmp.l   #32767,d2
        ble.s   .lo
        move.l  #32767,d2
.lo:    cmp.l   #-32768,d2
        bge.s   .idx
        move.l  #-32768,d2
.idx:   move.w  d7,d3                         ; indice += tabla[nibble & 7]
        and.w   #7,d3
        move.b  0(a3,d3.w),d3
        ext.w   d3
        add.w   d3,d3                         ; el indice se guarda por 2
        add.w   d3,d4
        bpl.s   .hi
        moveq   #0,d4
.hi:    cmp.w   #88*2,d4
        bls.s   .out
        move.w  #88*2,d4
.out:   move.w  d2,d3                         ; lo que suena: asr.w #8
        asr.w   #8,d3
        rts

        ifd     BENCH
;----------------------------------------------------------------------
; crc_buffer - suma al CRC las muestras *reales* del buffer que acaba de
; llenarse (a0). El relleno de silencio del final no cuenta: asi el numero
; tiene que dar exactamente igual al que calculo el encoder sobre las
; muestras de la cancion. Mismo CRC32 que a5_crc32 (encoder/stream.c).
;----------------------------------------------------------------------
crc_buffer:
        movem.l d0-d3/a0-a1,-(sp)
        move.l  V_CRCLEFT(a4),d1
        beq.s   .out
        cmp.l   #AUD_SAMPLES,d1               ; el ultimo buffer va cortado
        bls.s   .n
        move.l  #AUD_SAMPLES,d1
.n:     sub.l   d1,V_CRCLEFT(a4)
        move.l  a0,a1
        move.l  V_CRC(a4),d0
.byte:  moveq   #0,d2
        move.b  (a1)+,d2
        eor.l   d2,d0
        moveq   #7,d3
.bit:   lsr.l   #1,d0
        bcc.s   .nopoly
        eor.l   #$edb88320,d0
.nopoly:
        dbf     d3,.bit
        subq.l  #1,d1
        bne.s   .byte
        move.l  d0,V_CRC(a4)
.out:   movem.l (sp)+,d0-d3/a0-a1
        rts

;----------------------------------------------------------------------
; bench_finish - le devuelve la maquina al sistema y graba en el ultimo
; sector del disquete lo que midio, para leerlo desde el PC.
;----------------------------------------------------------------------
bench_finish:
        lea     CUSTOM,a0
        move.w  #$7fff,INTENA(a0)
        move.w  #$7fff,INTREQ(a0)
        move.l  V_OLDINT4(a4),$70.w
        move.w  V_OLDDMA(a4),d0
        and.w   #$ffdf,d0                     ; sin sprites: no hay punteros
        or.w    #$8000,d0
        move.w  d0,DMACON(a0)
        move.w  V_OLDINTENA(a4),d0
        or.w    #$c000,d0
        move.w  d0,INTENA(a0)
        move.l  4.w,a6
        jsr     _LVOPermit(a6)

        lea     infobuf(pc),a0
        move.l  #$4d555349,(a0)+              ; "MUSI"
        move.l  V_NSAMP(a4),(a0)+
        move.l  V_CRC(a4),d0
        not.l   d0                            ; el C invierte al salir
        move.l  d0,(a0)+
        move.l  V_ACOUNT(a4),(a0)+            ; buffers que pidio Paula
        move.l  V_CRCLEFT(a4),(a0)+           ; tiene que quedar en 0
        moveq   #0,d0
        move.w  V_AFMT(a4),d0
        move.l  d0,(a0)+
        move.w  V_APER(a4),d0
        move.l  d0,(a0)+
        move.l  V_BLK1(a4),(a0)+
        move.l  V_BLK1SIZE(a4),(a0)+
        move.l  V_BLK2(a4),(a0)+
        move.l  V_BLK2SIZE(a4),(a0)+
        move.l  V_PLANES(a4),(a0)+

        move.l  V_IOREQ(a4),a1
        move.w  #CMD_WRITE,IO_COMMAND(a1)
        move.l  #512,IO_LENGTH(a1)
        lea     infobuf(pc),a0
        move.l  a0,IO_DATA(a1)
        move.l  #INFO_SECTOR*512,IO_OFFSET(a1)
        jsr     _LVODoIO(a6)

        move.l  V_IOREQ(a4),a1                ; bajar la pista a disco
        move.w  #CMD_UPDATE,IO_COMMAND(a1)
        clr.l   IO_LENGTH(a1)
        jsr     _LVODoIO(a6)

        move.l  V_IOREQ(a4),a1                ; y apagar el motor
        move.w  #TD_MOTOR,IO_COMMAND(a1)
        clr.l   IO_LENGTH(a1)
        jsr     _LVODoIO(a6)
        rts
        endc

;----------------------------------------------------------------------
; Datos
;----------------------------------------------------------------------
        even
gfxname:    dc.b    "graphics.library",0
        even
header:     ds.b    HDR_SIZE
vars:       ds.b    VARS_SIZE
        even
idxtab:     dc.b    -1,-1,-1,-1,2,4,6,8
        even
steptab:
        dc.w        7,     8,     9,    10,    11,    12,    13,    14
        dc.w       16,    17,    19,    21,    23,    25,    28,    31
        dc.w       34,    37,    41,    45,    50,    55,    60,    66
        dc.w       73,    80,    88,    97,   107,   118,   130,   143
        dc.w      157,   173,   190,   209,   230,   253,   279,   307
        dc.w      337,   371,   408,   449,   494,   544,   598,   658
        dc.w      724,   796,   876,   963,  1060,  1166,  1282,  1411
        dc.w     1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024
        dc.w     3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484
        dc.w     7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899
        dc.w    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794
        dc.w    32767

        ifd     BENCH
        cnop    0,4
infobuf:    ds.b    512
        endc
