/*
 * Configuracion: rtxjuegos.ini (se crea con valores por defecto si no existe).
 * Se guarda en UTF-16 para que el Bloc de notas lo edite sin romper los acentos.
 */
#include "rtxjuegos.h"
#include <wchar.h>
#include <stdlib.h>

static const wchar_t INI_POR_DEFECTO[] =
    L"; ==========================================================\r\n"
    L";  RTXJuegos - configuración\r\n"
    L";  Editá este archivo con el Bloc de notas y volvé a abrir\r\n"
    L";  el programa (o tocá \"Jugar\": se relee cada vez).\r\n"
    L";  1 = sí, 0 = no. Las líneas con ; son comentarios.\r\n"
    L"; ==========================================================\r\n"
    L"\r\n"
    L"[general]\r\n"
    L"; Perfil que se usa al tocar Jugar (cualquier sección [perfil:Nombre])\r\n"
    L"perfil=Fluido\r\n"
    L"; auto = USB primero y si no hay, WiFi | usb = solo cable | wifi = solo inalámbrico\r\n"
    L"conexion=auto\r\n"
    L"; Si se desconecta el cable, esperar al celular y volver a abrir la imagen\r\n"
    L"reconectar=1\r\n"
    L"; Arrancar a jugar apenas se detecta el celular, sin tocar el botón\r\n"
    L"inicio_automatico=0\r\n"
    L"; Cerrar RTXJuegos cuando se cierra la ventana del juego\r\n"
    L"cerrar_al_terminar=0\r\n"
    L"; Versión de scrcpy que se descarga sola si falta la carpeta \"motor\"\r\n"
    L"version_scrcpy=" RTX_SCRCPY_DEF L"\r\n"
    L"; motor = usar el adb que viene con scrcpy | sistema = el adb del PATH | o una ruta a adb.exe\r\n"
    L"adb=motor\r\n"
    L"\r\n"
    L"; ----------------------------------------------------------\r\n"
    L";  PERFILES. Podés copiar uno, cambiarle el nombre y ajustarlo.\r\n"
    L";\r\n"
    L";  resolucion        lado más largo en píxeles (0 = la del celular)\r\n"
    L";  fps               cuadros por segundo máximos (0 = sin límite)\r\n"
    L";  bitrate           calidad del video: 8M, 16M, 24M...\r\n"
    L";  codec_video       h264 (menos demora) | h265 (más calidad)\r\n"
    L";  codificador_video vacío = automático (ej: c2.mtk.avc.encoder)\r\n"
    L";  decodificador     auto | d3d11va (placa de video) | disabled (procesador)\r\n"
    L";  buffer_video      ms de colchón (0 = mínima demora; 30-50 si hay tirones)\r\n"
    L";  audio             1 = pasar el sonido a la PC\r\n"
    L";  codec_audio       opus | aac | flac | raw\r\n"
    L";  bitrate_audio     128K, 192K...\r\n"
    L";  buffer_audio      ms (menos = menos demora; más = sin cortes). 40-60\r\n"
    L";  audio_en_celular  1 = que también suene en el celular (Android 13+)\r\n"
    L";  pantalla_completa 1 = pantalla completa (F11 la alterna)\r\n"
    L";  apagar_pantalla_celular 1 = apaga la pantalla del celular (el juego sigue)\r\n"
    L";  mantener_despierto 1 = el celular no se bloquea mientras está enchufado\r\n"
    L";  siempre_encima    1 = ventana siempre por encima\r\n"
    L";  sin_bordes        1 = ventana sin bordes (si no es pantalla completa)\r\n"
    L";  ajuste            letterbox (barras negras) | stretched (estirar) | unscaled\r\n"
    L";  orientacion       vacío = sigue al celular | @90 = fijar horizontal | @0 vertical\r\n"
    L";  mando_pc          no = el mando va al celular | uhid o aoa = usar un mando\r\n"
    L";                    conectado a la PC y mandarlo al celular\r\n"
    L";  apagar_celular_al_salir 1 = apaga el celular al cerrar\r\n"
    L";  formato_monitor   1 = mientras jugás, el celular pasa a la forma del monitor\r\n"
    L";                    (ej. 16:9) y la imagen llena TODA la pantalla sin deformarse.\r\n"
    L";                    Al terminar vuelve a su resolución original.\r\n"
    L";  extra             opciones de scrcpy adicionales tal cual (avanzado)\r\n"
    L"; ----------------------------------------------------------\r\n"
    L"\r\n"
    L"[perfil:Fluido]\r\n"
    L"resolucion=1920\r\n"
    L"fps=60\r\n"
    L"bitrate=16M\r\n"
    L"codec_video=h264\r\n"
    L"codificador_video=\r\n"
    L"decodificador=auto\r\n"
    L"buffer_video=0\r\n"
    L"audio=1\r\n"
    L"codec_audio=opus\r\n"
    L"bitrate_audio=128K\r\n"
    L"buffer_audio=50\r\n"
    L"audio_en_celular=0\r\n"
    L"pantalla_completa=1\r\n"
    L"apagar_pantalla_celular=1\r\n"
    L"mantener_despierto=1\r\n"
    L"formato_monitor=1\r\n"
    L"siempre_encima=0\r\n"
    L"sin_bordes=0\r\n"
    L"ajuste=letterbox\r\n"
    L"orientacion=\r\n"
    L"mando_pc=no\r\n"
    L"apagar_celular_al_salir=0\r\n"
    L"extra=\r\n"
    L"\r\n"
    L"[perfil:Competitivo 120Hz]\r\n"
    L"; Para monitores de 120/144 Hz y juegos rápidos. Usa más el celular.\r\n"
    L"resolucion=1600\r\n"
    L"fps=120\r\n"
    L"bitrate=20M\r\n"
    L"codec_video=h264\r\n"
    L"decodificador=auto\r\n"
    L"buffer_video=0\r\n"
    L"audio=1\r\n"
    L"codec_audio=opus\r\n"
    L"buffer_audio=40\r\n"
    L"pantalla_completa=1\r\n"
    L"apagar_pantalla_celular=1\r\n"
    L"mantener_despierto=1\r\n"
    L"formato_monitor=1\r\n"
    L"\r\n"
    L"[perfil:Calidad]\r\n"
    L"; Más nitidez, un poco más de demora. Ideal para juegos tranquilos o videos.\r\n"
    L"resolucion=0\r\n"
    L"fps=60\r\n"
    L"bitrate=24M\r\n"
    L"codec_video=h265\r\n"
    L"decodificador=auto\r\n"
    L"buffer_video=30\r\n"
    L"audio=1\r\n"
    L"codec_audio=opus\r\n"
    L"bitrate_audio=192K\r\n"
    L"buffer_audio=80\r\n"
    L"pantalla_completa=1\r\n"
    L"apagar_pantalla_celular=1\r\n"
    L"mantener_despierto=1\r\n"
    L"formato_monitor=1\r\n"
    L"\r\n"
    L"[perfil:Rapido]\r\n"
    L"; Para PC o celulares modestos, o cables/WiFi lentos.\r\n"
    L"resolucion=1280\r\n"
    L"fps=60\r\n"
    L"bitrate=8M\r\n"
    L"codec_video=h264\r\n"
    L"decodificador=auto\r\n"
    L"buffer_video=0\r\n"
    L"audio=1\r\n"
    L"codec_audio=opus\r\n"
    L"buffer_audio=40\r\n"
    L"pantalla_completa=1\r\n"
    L"apagar_pantalla_celular=1\r\n"
    L"mantener_despierto=1\r\n"
    L"formato_monitor=1\r\n"
    L"\r\n"
    L"[perfil:Ventana]\r\n"
    L"; Igual que Fluido pero en una ventana normal, siempre encima.\r\n"
    L"resolucion=1920\r\n"
    L"fps=60\r\n"
    L"bitrate=16M\r\n"
    L"codec_video=h264\r\n"
    L"audio=1\r\n"
    L"buffer_audio=50\r\n"
    L"pantalla_completa=0\r\n"
    L"siempre_encima=1\r\n"
    L"apagar_pantalla_celular=0\r\n"
    L"mantener_despierto=1\r\n"
    L"formato_monitor=0\r\n";

#define PREFIJO_PERFIL L"perfil:"

void config_asegurar_ini(void)
{
    HANDLE h;
    DWORD escrito;
    const WCHAR bom = 0xFEFF;

    if (archivo_existe(g_rutas.ini))
        return;
    h = CreateFileW(g_rutas.ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    WriteFile(h, &bom, sizeof bom, &escrito, NULL);
    WriteFile(h, INI_POR_DEFECTO, (DWORD)(sizeof INI_POR_DEFECTO - sizeof(wchar_t)), &escrito, NULL);
    CloseHandle(h);
    log_escribir(L"Se creó la configuración por defecto: %ls", g_rutas.ini);
}

static void leer_texto(const wchar_t *sec, const wchar_t *clave, const wchar_t *def, wchar_t *dest, DWORD cap)
{
    wchar_t *p;
    GetPrivateProfileStringW(sec, clave, def, dest, cap, g_rutas.ini);
    /* quitar comentario al final de la linea y espacios */
    p = wcschr(dest, L';');
    if (p)
        *p = 0;
    for (p = dest + wcslen(dest); p > dest && (p[-1] == L' ' || p[-1] == L'\t'); p--)
        p[-1] = 0;
}

static int leer_entero(const wchar_t *sec, const wchar_t *clave, int def)
{
    wchar_t buf[32];
    wchar_t defs[32];
    swprintf(defs, 32, L"%d", def);
    leer_texto(sec, clave, defs, buf, 32);
    return buf[0] ? _wtoi(buf) : def;
}

void config_leer_general(ConfigGeneral *g)
{
    wchar_t buf[32];

    leer_texto(L"general", L"perfil", L"Fluido", g->perfil, RTX_NOMBRE_LEN);
    leer_texto(L"general", L"conexion", L"auto", buf, 32);
    g->conexion = !_wcsicmp(buf, L"usb") ? CONEXION_USB : !_wcsicmp(buf, L"wifi") ? CONEXION_WIFI : CONEXION_AUTO;
    g->reconectar = leer_entero(L"general", L"reconectar", 1);
    g->inicio_automatico = leer_entero(L"general", L"inicio_automatico", 0);
    g->cerrar_al_terminar = leer_entero(L"general", L"cerrar_al_terminar", 0);
    leer_texto(L"general", L"version_scrcpy", RTX_SCRCPY_DEF, g->version_scrcpy, 32);
    if (!g->version_scrcpy[0])
        wcscpy(g->version_scrcpy, RTX_SCRCPY_DEF);
    leer_texto(L"general", L"adb", L"motor", g->adb, MAX_PATH);
}

void config_guardar_perfil_elegido(const wchar_t *nombre)
{
    WritePrivateProfileStringW(L"general", L"perfil", nombre, g_rutas.ini);
}

int config_listar_perfiles(wchar_t nombres[][RTX_NOMBRE_LEN], int max)
{
    static wchar_t secciones[16384];
    const size_t plen = wcslen(PREFIJO_PERFIL);
    wchar_t *s;
    int n = 0;

    if (!GetPrivateProfileSectionNamesW(secciones, 16384, g_rutas.ini))
        return 0;
    for (s = secciones; *s && n < max; s += wcslen(s) + 1) {
        if (!_wcsnicmp(s, PREFIJO_PERFIL, plen) && s[plen]) {
            wcsncpy(nombres[n], s + plen, RTX_NOMBRE_LEN - 1);
            nombres[n][RTX_NOMBRE_LEN - 1] = 0;
            n++;
        }
    }
    return n;
}

int config_leer_perfil(const wchar_t *nombre, Perfil *p)
{
    wchar_t sec[RTX_NOMBRE_LEN + 16];
    wchar_t prueba[8];

    swprintf(sec, RTX_NOMBRE_LEN + 16, PREFIJO_PERFIL L"%ls", nombre);
    ZeroMemory(p, sizeof *p);
    wcsncpy(p->nombre, nombre, RTX_NOMBRE_LEN - 1);

    /* las claves que falten toman el valor del perfil Fluido */
    p->resolucion              = leer_entero(sec, L"resolucion", 1920);
    p->fps                     = leer_entero(sec, L"fps", 60);
    leer_texto(sec, L"bitrate",           L"16M",      p->bitrate, 16);
    leer_texto(sec, L"codec_video",       L"h264",     p->codec_video, 16);
    leer_texto(sec, L"codificador_video", L"",         p->codificador_video, 64);
    leer_texto(sec, L"decodificador",     L"auto",     p->decodificador, 16);
    p->buffer_video            = leer_entero(sec, L"buffer_video", 0);
    p->audio                   = leer_entero(sec, L"audio", 1);
    leer_texto(sec, L"codec_audio",       L"opus",     p->codec_audio, 16);
    leer_texto(sec, L"bitrate_audio",     L"128K",     p->bitrate_audio, 16);
    p->buffer_audio            = leer_entero(sec, L"buffer_audio", 50);
    p->audio_en_celular        = leer_entero(sec, L"audio_en_celular", 0);
    p->pantalla_completa       = leer_entero(sec, L"pantalla_completa", 1);
    p->apagar_pantalla_celular = leer_entero(sec, L"apagar_pantalla_celular", 1);
    p->mantener_despierto      = leer_entero(sec, L"mantener_despierto", 1);
    p->siempre_encima          = leer_entero(sec, L"siempre_encima", 0);
    p->sin_bordes              = leer_entero(sec, L"sin_bordes", 0);
    leer_texto(sec, L"ajuste",            L"letterbox", p->ajuste, 16);
    leer_texto(sec, L"orientacion",       L"",          p->orientacion, 16);
    leer_texto(sec, L"mando_pc",          L"no",        p->mando_pc, 16);
    p->apagar_celular_al_salir = leer_entero(sec, L"apagar_celular_al_salir", 0);
    /* si falta la clave no se toca la resolucion: solo se cambia si el perfil lo pide */
    p->formato_monitor         = leer_entero(sec, L"formato_monitor", 0);
    /* 'extra' no pasa por leer_texto: puede contener ';' legitimamente */
    GetPrivateProfileStringW(sec, L"extra", L"", p->extra, 1024, g_rutas.ini);

    /* existe la seccion? */
    return GetPrivateProfileStringW(sec, NULL, L"", prueba, 8, g_rutas.ini) > 0;
}
