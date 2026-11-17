# RTXJuegos

Muestra la **pantalla y el sonido del celular en la PC, en pantalla completa y con poca demora**, para jugar en el monitor con un mando conectado al celular. Funciona por **USB** (o por WiFi si ya está emparejado) usando [scrcpy](https://github.com/Genymobile/scrcpy) y adb.

Solo para **Windows de 64 bits** (Windows 10 o 11).

## Cómo se usa

1. Copiá `rtxjuegos.exe` a cualquier carpeta (por ejemplo `C:\Juegos\RTXJuegos`).
2. Abrilo. **La primera vez descarga solo scrcpy + adb** (~11 MB, desde la página oficial de GitHub). Además comprueba la firma SHA256 para asegurarse de que el archivo no esté dañado ni alterado.
3. Conectá el celular por USB y aceptá el mensaje *"¿Permitir depuración USB?"*. O conectalo **por WiFi con el código QR** (ver abajo).
4. Elegí un perfil y tocá **Jugar**.

## Cómo encuentra el celular

Al abrirlo, RTXJuegos busca el celular solo, sin tener que usar adb en el CMD:

1. **Por cable USB.** Si en 6 segundos no aparece, reinicia adb (a veces no ve un celular recién enchufado) y vuelve a buscar.
2. **Por WiFi**, si el celular ya fue emparejado antes y tiene la *Depuración inalámbrica* activada: se conecta solo.
3. Si no hay nada, abre una ventana con un **código QR**. En el celular: *Opciones de desarrollador → Depuración inalámbrica →* **Vincular dispositivo con código QR**, y escaneá el código. RTXJuegos lo empareja, se conecta y cierra la ventana.

El botón **Conectar por WiFi (QR)** abre el código cuando quieras. El celular y la PC tienen que estar en la misma red; la primera vez Windows puede preguntar si deja que *adb.exe* use la red: tocá **Permitir**. Por WiFi hace falta Android 11 o superior.

Mientras jugás:

| Tecla | Qué hace |
|---|---|
| `F11` o `Alt+F` | Activa o desactiva la pantalla completa |
| `Alt+F4` | Termina la sesión (vuelve a la ventana de RTXJuegos) |
| `Alt+O` | Apaga la pantalla del celular (la imagen sigue) |
| `Alt+Shift+O` | Vuelve a encender la pantalla del celular |

Si el cable se desconecta, RTXJuegos espera y **vuelve a abrir la imagen sola** cuando lo reconectás.

## Pantalla completa de verdad (sin barras negras)

Los celulares son más alargados que los monitores: 20:9 contra 16:9. Si no se hace nada, quedan barras negras arriba y abajo. Con `formato_monitor=1`, que viene activado en todos los perfiles salvo "Ventana":

1. Al tocar Jugar, el celular pasa a la **misma forma que el monitor** (ej. 720×1594 → 720×1280).
2. El juego se dibuja en esa forma, y la imagen **llena todo el monitor, sin deformarse ni recortarse**.
3. Al terminar, el celular **vuelve a su resolución original**. Si el programa o la PC se cerraron de golpe, la restaura la próxima vez que lo abras y conectes ese celular (queda anotado en `restaurar.ini`).

> Si el juego ya estaba abierto antes de tocar Jugar y no llena la pantalla, cerralo y volvé a abrirlo.

## Preparar el celular (una sola vez)

1. *Ajustes → Sobre el teléfono →* tocá 7 veces **Versión de HyperOS / MIUI** (o "Número de compilación" en otras marcas).
2. *Ajustes → Ajustes adicionales → Opciones de desarrollador →* activá:
   - **Depuración USB**
   - **Depuración USB (ajustes de seguridad)**. Solo en **Xiaomi/Redmi/POCO**: sin esto el teclado y el mouse de la PC no controlan el celular. Hay que **reiniciar el celular** después de activarlo.
3. El sonido necesita **Android 11 o superior**. En Android 11, el celular tiene que estar desbloqueado al tocar Jugar.

## El mando

- **Mando Bluetooth al celular** (recomendado): como el cable USB ya ocupa el puerto del celular, conectá el mando por Bluetooth y jugá normal.
- **Mando conectado a la PC**: en el perfil poné `mando_pc=uhid`. El mando que enchufes a la PC se envía al celular como si fuera físico.

## Personalizar: `rtxjuegos.ini`

Tocá **Editar perfiles**. Se abre el Bloc de notas con todas las opciones explicadas. Podés copiar un perfil, cambiarle el nombre (`[perfil:MiPerfil]`) y aparece en la lista. Perfiles incluidos:

| Perfil | Resolución | FPS | Calidad | Para qué |
|---|---|---|---|---|
| **Fluido** | 1920 | 60 | 16M H.264 | El de siempre |
| **Competitivo 120Hz** | 1600 | 120 | 20M H.264 | Solo para monitores de 120/144 Hz (en uno de 60 Hz no se nota) |
| **Calidad** | nativa | 60 | 24M H.265 | Más nitidez, un poco más de demora |
| **Rapido** | 1280 | 60 | 8M H.264 | PC o celular modestos |
| **Ventana** | 1920 | 60 | 16M H.264 | Ventana normal, siempre encima |

Opciones generales: `conexion=auto|usb|wifi`, `reconectar`, `inicio_automatico` (arranca apenas detecta el celular), `cerrar_al_terminar`, `version_scrcpy` (para actualizar scrcpy: cambiá el número y se descarga solo), `adb=motor|sistema|ruta`.

Si algo falla, **Ver registro** abre `rtxjuegos.log` con todo lo que pasó.

### Arreglos automáticos

- Si el audio no arranca con Opus, reintenta con **AAC** y después con **raw** (pasa en algunos Xiaomi con Android 15; ver [scrcpy#5452](https://github.com/Genymobile/scrcpy/issues/5452)).
- Si el celular tiene Android 10 o anterior, abre solo la imagen y te avisa.
- Si la conexión se corta 3 veces seguidas apenas empieza, deja de reintentar y te sugiere cambiar el cable o el puerto.

## Compartirlo

Pasá **solo `rtxjuegos.exe`**. En la otra PC se crean solos `rtxjuegos.ini` y la carpeta `motor` (scrcpy + adb). Si esa PC no tiene Internet, copiá también la carpeta `motor`.

## Compilar desde el código

Requisitos: [MinGW-w64](https://www.mingw-w64.org/) (gcc + windres). Python + Pillow solo si querés regenerar el ícono.

```
build.bat
```

Resultado: `dist\rtxjuegos.exe` (~100 KB, sin DLLs extra).

```
src\main.c         ventana y hilo de trabajo
src\config.c       rtxjuegos.ini y perfiles
src\motor.c        descarga + SHA256 + descompresión de scrcpy/adb
src\dispositivo.c  detección del celular (adb devices / getprop), WiFi (mdns, connect, pair)
src\qr.c           ventana del código QR para emparejar por WiFi
src\qrcodegen.c    generador de códigos QR (Project Nayuki, MIT)
src\sesion.c       opciones de scrcpy, ejecución y registro
src\formato.c      formato del monitor (wm size) y restauración
recursos\          ícono, manifiesto y datos de versión
herramientas\crear_icono.py   genera el ícono (Python + Pillow)
```

## Licencias

scrcpy es de Genymobile y tiene licencia Apache 2.0 (`motor\LICENSE.txt`). adb es parte de Android SDK Platform-Tools. `src\qrcodegen.c/.h` es de Project Nayuki y tiene licencia MIT (el aviso está al principio del archivo).
