<#
    build.ps1 - construccion de A500VP.

        .\build.ps1                 ensambla y compila todo
        .\build.ps1 disk -Video X   el disco de verdad: work\a500vp.adf
        .\build.ps1 run             arranca en WinUAE el disco del Hito 0
        .\build.ps1 check           ...y lee la medicion de memoria
        .\build.ps1 still           Hito 3: frame fijo, verificado byte a byte
        .\build.ps1 play            Hito 4/5: disco de medicion
        .\build.ps1 clean           borra work\

    Rutas por defecto detectadas en esta maquina; se pueden pisar con
    -Vasm, -Gcc, -WinUAE, -Rom.
#>
[CmdletBinding()]
param(
    [ValidateSet('build', 'run', 'check', 'still', 'play', 'disk', 'clean')]
    [string]$Task = 'build',

    [string]$Vasm   = 'C:\Users\JC\vbcc\bin\vasmm68k_mot.exe',
    [string]$Gcc    = 'C:\msys64\ucrt64\bin\gcc.exe',
    [string]$WinUAE = 'C:\Program Files\WinUAE\winuae64.exe',
    [string]$Rom    = '',

    # Segundos que dejamos correr el emulador antes de cerrarlo. El Hito 0
    # solo arranca y mide, con lo que unos pocos alcanzan.
    [int]$Timeout = 25,

    # disk: el video fuente y cuanto tomar (los primeros 22 s, Hito 2).
    [string]$Video = '',
    [double]$Duration = 22,

    # still / play: el bitstream que se usa (el que deja 'disk'), y para
    # still, cual frame.
    [string]$Stream = 'work\video.a5v',
    [int]$Frame = 182,

    # Hito 4: cuanto dejar correr el disco de medicion (carga + video +
    # grabacion de mediciones) y cada cuanto capturar la ventana.
    [int]$PlayWait = 150,
    [int]$ShotEvery = 10
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$work = Join-Path $root 'work'

# El ultimo sector del disquete lleva la medicion que graba el reproductor.
$RESULT_SECTOR = 1759
$SECTOR_SIZE   = 512
# El disco de medicion (player.s -DBENCH=1) graba en los sectores 1754..1759.
$BenchTail     = 6

function Invoke-Tool {
    param([string]$Exe, [string[]]$Arguments)
    Write-Host "+ $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "fallo: $Exe (codigo $LASTEXITCODE)" }
}

function Get-BE32 {
    param([byte[]]$Bytes, [int]$Offset)
    return ([uint32]$Bytes[$Offset]     -shl 24) -bor
           ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
           ([uint32]$Bytes[$Offset + 2] -shl 8)  -bor
           ([uint32]$Bytes[$Offset + 3])
}

#---------------------------------------------------------------------------
function Build-All {
    if (-not (Test-Path $Vasm)) { throw "No encuentro vasm en '$Vasm'. Pasa -Vasm <ruta>." }
    if (-not (Test-Path $Gcc))  { throw "No encuentro gcc en '$Gcc'. Pasa -Gcc <ruta>." }
    if (-not (Test-Path $work)) { New-Item -ItemType Directory $work | Out-Null }

    $player = Join-Path $root 'player'

    Invoke-Tool $Vasm @('-Fbin', '-m68000', '-no-opt', '-I', $player,
                        '-o', (Join-Path $work 'boot.bin'),
                        (Join-Path $player 'boot.s'))

    Invoke-Tool $Vasm @('-Fbin', '-m68000', '-no-opt', '-I', $player,
                        '-o', (Join-Path $work 'memcheck.bin'),
                        (Join-Path $player 'memcheck.s'))

    # Hito 3: disco de prueba del frame fijo (regresion del decodificador)
    Invoke-Tool $Vasm @('-Fbin', '-m68000', '-no-opt', '-I', $player,
                        '-o', (Join-Path $work 'still.bin'),
                        (Join-Path $player 'still.s'))

    # El reproductor, y su version de medicion (-DBENCH)
    Invoke-Tool $Vasm @('-Fbin', '-m68000', '-no-opt', '-I', $player,
                        '-o', (Join-Path $work 'player.bin'),
                        (Join-Path $player 'player.s'))

    Invoke-Tool $Vasm @('-Fbin', '-m68000', '-no-opt', '-I', $player,
                        '-DBENCH=1',
                        '-o', (Join-Path $work 'player_bench.bin'),
                        (Join-Path $player 'player.s'))

    Invoke-Tool $Gcc @('-std=c11', '-O2', '-Wall', '-Wextra', '-pedantic',
                       (Join-Path $root 'encoder\adf.c'),
                       (Join-Path $root 'encoder\mkadf.c'),
                       '-o', (Join-Path $work 'mkadf.exe'))

    $shared = @((Join-Path $root 'encoder\color.c'),
                (Join-Path $root 'encoder\quant.c'),
                (Join-Path $root 'encoder\stream.c'),
                (Join-Path $root 'encoder\vio.c'))
    $cflags = @('-std=c11', '-O2', '-Wall', '-Wextra', '-pedantic')

    Invoke-Tool $Gcc ($cflags + $shared + @((Join-Path $root 'encoder\encode.c'),
                       (Join-Path $root 'encoder\adf.c'),
                       '-o', (Join-Path $work 'a500vp-enc.exe'), '-lm'))

    Invoke-Tool $Gcc ($cflags + $shared + @((Join-Path $root 'encoder\decode.c'),
                       '-o', (Join-Path $work 'a500vp-dec.exe'), '-lm'))

    # Disco de musica (A5MU): encoder y decoder de referencia
    $music = @((Join-Path $root 'encoder\adpcm.c'), (Join-Path $root 'encoder\ham.c'))
    Invoke-Tool $Gcc ($cflags + $shared + $music + @((Join-Path $root 'encoder\music.c'),
                       (Join-Path $root 'encoder\adf.c'),
                       '-o', (Join-Path $work 'a5mu-enc.exe'), '-lm'))
    Invoke-Tool $Gcc ($cflags + $shared + $music + @((Join-Path $root 'encoder\musicdec.c'),
                       '-o', (Join-Path $work 'a5mu-dec.exe'), '-lm'))

    Invoke-Tool (Join-Path $work 'mkadf.exe') @(
        '--boot',   (Join-Path $work 'boot.bin'),
        '--player', (Join-Path $work 'memcheck.bin'),
        '--out',    (Join-Path $work 'memcheck.adf'))
}

#---------------------------------------------------------------------------
# Genera una copia del .uae del repo con la ROM y el ADF puestos, y lanza
# WinUAE. Devuelve la ruta del ADF que uso (una copia: el reproductor le
# graba encima y no queremos ensuciar el original).
function Start-Emulator {
    param([switch]$Headless, [string]$AdfPath = '')

    if (-not (Test-Path $WinUAE)) { throw "No encuentro WinUAE en '$WinUAE'." }

    $romPath = $Rom
    if (-not $romPath) {
        $candidate = Join-Path $root 'kick12.rom'
        if (Test-Path $candidate) { $romPath = $candidate }
    }
    if (-not $romPath -or -not (Test-Path $romPath)) {
        throw "Falta la ROM de Kickstart. Pasa -Rom <ruta> o deja kick12.rom en la raiz."
    }

    $adf = if ($AdfPath) { $AdfPath } else { Join-Path $work 'memcheck.adf' }
    if (-not (Test-Path $adf)) { throw "No hay ADF en '$adf'; corre '.\build.ps1' primero." }
    $runAdf = Join-Path $work 'run.adf'
    Copy-Item $adf $runAdf -Force

    $cfgIn  = Join-Path $root 'a500vp.uae'
    $cfgOut = Join-Path $work 'a500vp.run.uae'
    $lines = Get-Content $cfgIn | ForEach-Object {
        if ($_ -like 'kickstart_rom_file=*') { "kickstart_rom_file=$romPath" }
        elseif ($_ -like 'floppy0=*')        { "floppy0=$runAdf" }
        else                                 { $_ }
    }
    if ($Headless) { $lines += 'use_gui=no' }
    Set-Content -Path $cfgOut -Value $lines -Encoding ascii

    Write-Host "ROM : $romPath"
    Write-Host "ADF : $runAdf"
    $proc = Start-Process -FilePath $WinUAE -ArgumentList @('-f', "`"$cfgOut`"") -PassThru
    return @{ Process = $proc; Adf = $runAdf }
}

#---------------------------------------------------------------------------
function Show-Measurement {
    param([string]$Adf)

    $bytes = [System.IO.File]::ReadAllBytes($Adf)
    $off = $RESULT_SECTOR * $SECTOR_SIZE
    $magic = [System.Text.Encoding]::ASCII.GetString($bytes, $off, 4)

    if ($magic -ne 'MEMR') {
        Write-Host ""
        Write-Host "El reproductor no grabo la medicion (magic='$magic')." -ForegroundColor Yellow
        Write-Host "Mira la pantalla del emulador: los valores se muestran igual."
        return $false
    }

    $names = @('CHIP libre', 'CHIP mayor bloque', 'SLOW/FAST libre',
               'SLOW/FAST mayor bloque', 'TOTAL libre')
    Write-Host ""
    Write-Host "Memoria libre medida con AvailMem desde el reproductor:" -ForegroundColor Green
    for ($i = 0; $i -lt 5; $i++) {
        $v = Get-BE32 $bytes ($off + 4 + $i * 4)
        Write-Host ("  {0,-24} {1,10} bytes  {2,7} KB  (0x{3:X8})" -f
                    $names[$i], $v, [math]::Round($v / 1024, 1), $v)
    }
    $s2 = Get-BE32 $bytes ($off + 24)
    $bp = Get-BE32 $bytes ($off + 28)
    Write-Host ("  reproductor cargado en   0x{0:X8}" -f $s2)
    Write-Host ("  bitplane asignado en     0x{0:X8}" -f $bp)
    return $true
}

#---------------------------------------------------------------------------
function Get-BE16 {
    param([byte[]]$Bytes, [int]$Offset)
    return ([int]$Bytes[$Offset] -shl 8) -bor [int]$Bytes[$Offset + 1]
}

# Cierra WinUAE por la ventana, no matandolo: solo al salir ordenadamente
# vuelca al .adf lo que la Amiga escribio en el disquete.
function Stop-Emulator {
    param($Process)
    if ($Process.HasExited) { return }
    $Process.CloseMainWindow() | Out-Null
    if (-not $Process.WaitForExit(15000)) {
        Write-Host "WinUAE no cerro solo; lo mato (el ADF puede no tener lo escrito)." -ForegroundColor Yellow
        $Process.Kill()
        $Process.WaitForExit(10000) | Out-Null
    }
    Start-Sleep -Milliseconds 500
}

# Captura la ventana de WinUAE con PrintWindow: la ventana se dibuja sola en
# un bitmap. No copia pixeles de la pantalla, asi que no importa que haya
# encima y nunca puede capturar otra aplicacion. (Copiar la pantalla fallo:
# Windows no deja traer al frente una ventana desde un proceso de fondo.)
function Save-WindowShot {
    param($Process, [string]$Out)
    Add-Type -AssemblyName System.Drawing
    if (-not ('A5Win' -as [type])) {
        Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class A5Win {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
    }
    [A5Win]::SetProcessDPIAware() | Out-Null
    $Process.Refresh()
    $h = $Process.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { Write-Host "WinUAE no tiene ventana para capturar."; return }
    $r = New-Object A5Win+RECT
    [A5Win]::GetWindowRect($h, [ref]$r) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    # 2 = PW_RENDERFULLCONTENT: hace falta para ventanas Direct3D como WinUAE.
    $ok = [A5Win]::PrintWindow($h, $hdc, 2)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    if (-not $ok) { $bmp.Dispose(); Write-Host "PrintWindow fallo: sin captura." -ForegroundColor Yellow; return }
    $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "captura    : $Out"
}

# Compara lo que el reproductor volco al disquete contra lo que tiene que
# dar segun el decoder de referencia. Formato del volcado en FORMAT.md.
function Test-Still {
    param([string]$Adf, [string]$Still)
    # El decoder de referencia compara framebuffer y copper list, byte por
    # byte, con lo que tendria que haber armado el reproductor.
    & (Join-Path $work 'a500vp-dec.exe') --in $Still --check-still $Adf |
        Select-String -Pattern 'volcado|reproductor:|delta  |framebuffer:|copper list:|AVISO|Mira' |
        ForEach-Object { Write-Host $_.Line }
    return ($LASTEXITCODE -eq 0)
}

#---------------------------------------------------------------------------
switch ($Task) {
    'clean' {
        if (Test-Path $work) { Remove-Item $work -Recurse -Force }
        Write-Host "work\ borrado"
    }

    'build' { Build-All }

    'run' {
        Build-All
        $r = Start-Emulator
        Write-Host "WinUAE lanzado. Cerralo vos cuando termines de mirar."
    }

    'still' {
        Build-All
        $streamPath = if ([System.IO.Path]::IsPathRooted($Stream)) { $Stream } else { Join-Path $root $Stream }
        if (-not (Test-Path $streamPath)) {
            throw "No existe $streamPath. Generalo con a500vp-enc (ver docs\SETUP.md)."
        }
        $still = Join-Path $work 'still.a5v'
        Invoke-Tool (Join-Path $work 'a500vp-dec.exe') @('--in', $streamPath,
            '--still', "$Frame", '--still-out', $still)
        $adf = Join-Path $work 'still.adf'
        Invoke-Tool (Join-Path $work 'mkadf.exe') @(
            '--boot',   (Join-Path $work 'boot.bin'),
            '--player', (Join-Path $work 'still.bin'),
            '--data',   $still,
            '--reserve-tail', '80',
            '--out',    $adf)

        $r = Start-Emulator -Headless -AdfPath $adf
        Write-Host "Esperando $Timeout s a que arranque, decodifique y vuelque..."
        Start-Sleep -Seconds $Timeout
        Save-WindowShot $r.Process (Join-Path $work 'still_amiga.png')
        Stop-Emulator $r.Process
        & ffmpeg -v error -y -i "$still.ppm" (Join-Path $work 'still_ref.png')
        Write-Host "referencia : $(Join-Path $work 'still_ref.png')"
        if (-not (Test-Still $r.Adf $still)) { exit 1 }
    }

    'disk' {
        # El disco de verdad: el encoder arma el bitstream y el .adf completo
        # (bootblock + reproductor + datos); el decoder de referencia lo
        # verifica y saca el preview con el audio tal como va a sonar.
        Build-All
        if (-not $Video) { throw "Falta -Video <ruta al video fuente>." }
        $out = Join-Path $work 'video.a5v'
        $adf = Join-Path $work 'a500vp.adf'
        # El mismo video.a5v tiene que entrar tambien en el disco de medicion
        # (tarea play): su reproductor es mas largo y escribe los resultados
        # en los ultimos $BenchTail sectores. Se reserva justo esa diferencia.
        $pl = (Get-Item (Join-Path $work 'player.bin')).Length
        $pb = (Get-Item (Join-Path $work 'player_bench.bin')).Length
        $tail = $BenchTail + [math]::Ceiling($pb / 512) - [math]::Ceiling($pl / 512)
        Invoke-Tool (Join-Path $work 'a500vp-enc.exe') @('--in', $Video,
            '--duration', "$Duration", '--out', $out, '--adf', $adf,
            '--boot', (Join-Path $work 'boot.bin'),
            '--player', (Join-Path $work 'player.bin'),
            '--reserve-tail', "$tail")
        Invoke-Tool (Join-Path $work 'a500vp-dec.exe') @('--in', $out,
            '--preview', (Join-Path $work 'preview.mp4'))
        Write-Host "disco      : $adf"
    }

    'play' {
        Build-All
        $streamPath = if ([System.IO.Path]::IsPathRooted($Stream)) { $Stream } else { Join-Path $root $Stream }
        if (-not (Test-Path $streamPath)) {
            throw "No existe $streamPath. Generalo con a500vp-enc (ver docs\SETUP.md)."
        }
        $adf = Join-Path $work 'play.adf'
        Invoke-Tool (Join-Path $work 'mkadf.exe') @(
            '--boot',   (Join-Path $work 'boot.bin'),
            '--player', (Join-Path $work 'player_bench.bin'),
            '--data',   $streamPath,
            '--reserve-tail', "$BenchTail",
            '--out',    $adf)

        # Una carpeta con marca de tiempo por corrida, para no mezclar
        # capturas viejas con nuevas.
        $shots = Join-Path $work ('shots\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        New-Item -ItemType Directory -Force $shots | Out-Null
        $r = Start-Emulator -Headless -AdfPath $adf
        Write-Host "Corriendo $PlayWait s (carga + reproduccion + grabacion de mediciones)..."
        $t0 = Get-Date
        while ((((Get-Date) - $t0).TotalSeconds -lt $PlayWait) -and -not $r.Process.HasExited) {
            Start-Sleep -Seconds $ShotEvery
            $sec = [int]((Get-Date) - $t0).TotalSeconds
            Save-WindowShot $r.Process (Join-Path $shots ('{0:d3}s.png' -f $sec)) | Out-Null
        }
        Write-Host "capturas   : $shots"
        Stop-Emulator $r.Process
        Invoke-Tool (Join-Path $work 'a500vp-dec.exe') @('--in', $streamPath, '--measure', $r.Adf)
    }

    'check' {
        Build-All
        $r = Start-Emulator -Headless
        Write-Host "Esperando $Timeout s a que arranque y mida..."
        $null = $r.Process.WaitForExit($Timeout * 1000)
        if (-not $r.Process.HasExited) {
            # Cerrar por la ventana, no matarlo: WinUAE mantiene la pista
            # escrita en memoria y solo vuelca el .adf al salir ordenadamente.
            $r.Process.CloseMainWindow() | Out-Null
            if (-not $r.Process.WaitForExit(15000)) {
                Write-Host "WinUAE no cerro solo; lo mato (el ADF puede no tener la medicion)." -ForegroundColor Yellow
                $r.Process.Kill()
                $r.Process.WaitForExit(10000) | Out-Null
            }
        }
        Start-Sleep -Milliseconds 500
        if (-not (Show-Measurement $r.Adf)) { exit 1 }
    }
}
