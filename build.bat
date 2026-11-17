@echo off
rem Compila RTXJuegos (Windows 64 bits) con MinGW-w64 (gcc + windres).
rem Resultado: dist\rtxjuegos.exe  -- la carpeta "motor" (scrcpy + adb) se descarga sola al abrirlo.
setlocal
cd /d "%~dp0"

if not exist dist mkdir dist
if not exist obj mkdir obj

if not exist recursos\rtxjuegos.ico (
    echo Generando el icono con Python...
    python herramientas\crear_icono.py || goto :error
)

windres -O coff -I recursos recursos\rtxjuegos.rc -o obj\recursos.o || goto :error

gcc -std=c11 -O2 -s -Wall -Wextra -Wno-missing-field-initializers -municode -mwindows -static ^
    -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 ^
    src\main.c src\config.c src\util.c src\motor.c src\dispositivo.c src\sesion.c src\formato.c src\qr.c src\qrcodegen.c obj\recursos.o ^
    -o dist\rtxjuegos.exe ^
    -lwininet -lbcrypt -lcomctl32 -lshell32 -luser32 -lgdi32 || goto :error

echo.
echo Listo: dist\rtxjuegos.exe
exit /b 0

:error
echo.
echo *** Fallo la compilacion ***
exit /b 1
