/*
 * Deteccion del celular con adb: estado, tipo de conexion, modelo y version de Android.
 * Tambien la conexion por WiFi: celulares ya emparejados (mDNS + adb connect) y
 * emparejamiento nuevo con el QR de "Depuracion inalambrica" (adb pair).
 */
#include "rtxjuegos.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static char s_salida[16384];
static Dispositivo s_cache;

/* deja en el registro la primera linea de la salida de adb (viene en UTF-8) */
static void log_adb(const wchar_t *que, char *salida)
{
    wchar_t texto[512];
    salida[strcspn(salida, "\r\n")] = 0;
    MultiByteToWideChar(CP_UTF8, 0, salida, -1, texto, 512);
    texto[511] = 0;
    log_escribir(L"%ls -> %ls", que, texto);
}

void dispositivo_iniciar_adb(const wchar_t *adb)
{
    wchar_t cmd[MAX_PATH + 32];
    DWORD codigo;
    swprintf(cmd, MAX_PATH + 32, L"\"%ls\" start-server", adb);
    ejecutar_capturando(cmd, s_salida, sizeof s_salida, 20000, &codigo);
    log_escribir(L"adb start-server -> %lu", codigo);
}

void dispositivo_reiniciar_adb(const wchar_t *adb)
{
    wchar_t cmd[MAX_PATH + 32];
    DWORD codigo;
    swprintf(cmd, MAX_PATH + 32, L"\"%ls\" kill-server", adb);
    ejecutar_capturando(cmd, s_salida, sizeof s_salida, 10000, &codigo);
    log_escribir(L"adb kill-server -> %lu", codigo);
    Sleep(500);
    dispositivo_iniciar_adb(adb);
    /* el servidor nuevo tarda un poco en ver los celulares conectados por USB */
    Sleep(2000);
}

void dispositivo_reconectar_offline(const wchar_t *adb)
{
    wchar_t cmd[MAX_PATH + 32];
    DWORD codigo;
    swprintf(cmd, MAX_PATH + 32, L"\"%ls\" reconnect offline", adb);
    ejecutar_capturando(cmd, s_salida, sizeof s_salida, 10000, &codigo);
    log_escribir(L"adb reconnect offline -> %lu", codigo);
}

/* Separa una linea de adb en campos. Van separados por tabulador: los nombres por WiFi pueden
   tener espacios ("adb-XXXX (2)._adb-tls-connect._tcp" cuando el celular se vuelve a anunciar). */
static int partir_campos(char *linea, char **campos, int max)
{
    int n = 0;
    char *p = linea;
    while (n < max) {
        char *fin = strchr(p, '\t');
        campos[n++] = p;
        if (!fin)
            break;
        *fin = 0;
        p = fin + 1;
    }
    return n;
}

static int serial_es_wifi(const char *serial)
{
    /* "192.168.1.5:5555" o los nombres mDNS "adb-XXXX._adb-tls-connect._tcp" */
    return strchr(serial, ':') != NULL || strstr(serial, "._tcp") != NULL;
}

static void shell_getprop(const wchar_t *adb, const wchar_t *serial, const wchar_t *prop, wchar_t *dest, size_t cap)
{
    wchar_t cmd[MAX_PATH + 256];
    DWORD codigo;
    char *p;

    dest[0] = 0;
    swprintf(cmd, MAX_PATH + 256, L"\"%ls\" -s \"%ls\" shell getprop %ls", adb, serial, prop);
    if (!ejecutar_capturando(cmd, s_salida, sizeof s_salida, 8000, &codigo) || codigo != 0)
        return;
    for (p = s_salida; *p; p++)
        if (*p == '\r' || *p == '\n') {
            *p = 0;
            break;
        }
    MultiByteToWideChar(CP_UTF8, 0, s_salida, -1, dest, (int)cap);
}

void dispositivo_buscar(const wchar_t *adb, Conexion conexion, Dispositivo *d)
{
    wchar_t cmd[MAX_PATH + 32];
    DWORD codigo;
    char *linea, *ctx = NULL;
    /* mejor candidato de cada tipo */
    char usb_ok[128] = "", wifi_ok[128] = "";
    int hay_sin_permiso = 0, hay_conectando = 0, hay_offline = 0, hay_otra = 0;
    wchar_t sdk[16];
    /* lo que respondio adb, para dejarlo en el registro cada vez que cambia */
    static char lista_previa[1024] = "?";
    char lista[1024] = "";

    ZeroMemory(d, sizeof *d);
    d->estado = DISP_NINGUNO;

    swprintf(cmd, MAX_PATH + 32, L"\"%ls\" devices", adb);
    if (!ejecutar_capturando(cmd, s_salida, sizeof s_salida, 15000, &codigo)) {
        log_escribir(L"No se pudo ejecutar adb (error %lu)", GetLastError());
        return;
    }

    for (linea = strtok_s(s_salida, "\r\n", &ctx); linea; linea = strtok_s(NULL, "\r\n", &ctx)) {
        char serial[128], estado[64], *campos[2];
        int wifi, permitido;
        if (strncmp(linea, "List of", 7) == 0 || linea[0] == '*')
            continue;
        if (partir_campos(linea, campos, 2) != 2 || !campos[0][0] || strlen(campos[0]) >= sizeof serial ||
            sscanf(campos[1], "%63s", estado) != 1)
            continue;
        strcpy(serial, campos[0]);
        if (strlen(lista) + strlen(serial) + strlen(estado) + 4 < sizeof lista) {
            strcat(lista, lista[0] ? " | " : "");
            strcat(lista, serial);
            strcat(lista, " ");
            strcat(lista, estado);
        }
        wifi = serial_es_wifi(serial);
        permitido = conexion == CONEXION_AUTO || (conexion == CONEXION_USB && !wifi) ||
                    (conexion == CONEXION_WIFI && wifi);
        if (!permitido) {
            if (!strcmp(estado, "device"))
                hay_otra = 1;
            continue;
        }
        if (!strcmp(estado, "device")) {
            if (wifi && !wifi_ok[0])
                strcpy(wifi_ok, serial);
            else if (!wifi && !usb_ok[0])
                strcpy(usb_ok, serial);
        } else if (!strcmp(estado, "unauthorized")) {
            hay_sin_permiso = 1;
        } else if (!strcmp(estado, "authorizing") || !strcmp(estado, "connecting")) {
            /* se esta conectando recien: no hay que hacer nada, solo esperar */
            hay_conectando = 1;
        } else {
            hay_offline = 1;
        }
    }
    if (strcmp(lista, lista_previa)) {
        strcpy(lista_previa, lista);
        log_escribir(L"adb devices: %hs", lista[0] ? lista : "(ninguno)");
    }

    if (usb_ok[0] || wifi_ok[0]) {
        const char *elegido = usb_ok[0] ? usb_ok : wifi_ok; /* USB tiene prioridad */
        d->estado = DISP_LISTO;
        d->es_usb = usb_ok[0] != 0;
        MultiByteToWideChar(CP_UTF8, 0, elegido, -1, d->serial, 128);

        /* Se consulta cada ~1,5 s: los datos del mismo celular se guardan y no se vuelven a pedir */
        if (s_cache.serial[0] && !wcscmp(s_cache.serial, d->serial)) {
            wcscpy(d->modelo, s_cache.modelo);
            wcscpy(d->android, s_cache.android);
            wcscpy(d->id, s_cache.id);
            d->sdk = s_cache.sdk;
            return;
        }
        shell_getprop(adb, d->serial, L"ro.product.marketname", d->modelo, 128);
        if (!d->modelo[0])
            shell_getprop(adb, d->serial, L"ro.product.model", d->modelo, 128);
        shell_getprop(adb, d->serial, L"ro.build.version.release", d->android, 16);
        shell_getprop(adb, d->serial, L"ro.build.version.sdk", sdk, 16);
        d->sdk = _wtoi(sdk);
        shell_getprop(adb, d->serial, L"ro.serialno", d->id, 128);
        if (!d->id[0])
            wcscpy(d->id, d->serial);
        if (d->sdk > 0)
            s_cache = *d;
    } else if (hay_sin_permiso) {
        d->estado = DISP_SIN_PERMISO;
    } else if (hay_conectando) {
        d->estado = DISP_CONECTANDO;
    } else if (hay_offline) {
        d->estado = DISP_OFFLINE;
    } else if (hay_otra) {
        d->estado = DISP_SOLO_OTRA_CONEXION;
    }
}

/* ---------- WiFi ---------- */

typedef struct {
    char nombre[128];   /* "adb-<serial>-xxxx" o, al emparejar, el nombre que va en el QR */
    char tipo[64];      /* _adb-tls-connect._tcp | _adb-tls-pairing._tcp */
    char dir[64];       /* ip:puerto */
} ServicioMdns;

/* Celulares que se anuncian en la red con la "Depuracion inalambrica" activada */
static int mdns_listar(const wchar_t *adb, ServicioMdns *s, int max)
{
    wchar_t cmd[MAX_PATH + 32];
    DWORD codigo;
    char *linea, *ctx = NULL;
    int n = 0;

    swprintf(cmd, MAX_PATH + 32, L"\"%ls\" mdns services", adb);
    if (!ejecutar_capturando(cmd, s_salida, sizeof s_salida, 8000, &codigo))
        return 0;
    for (linea = strtok_s(s_salida, "\r\n", &ctx); linea && n < max; linea = strtok_s(NULL, "\r\n", &ctx)) {
        char *campos[3];
        if (strncmp(linea, "List of", 7) == 0 || linea[0] == '*')
            continue;
        if (partir_campos(linea, campos, 3) != 3 || strlen(campos[0]) >= sizeof s[n].nombre ||
            sscanf(campos[1], "%63s", s[n].tipo) != 1 || sscanf(campos[2], "%63s", s[n].dir) != 1 ||
            !strchr(s[n].dir, ':'))
            continue;
        strcpy(s[n].nombre, campos[0]);
        n++;
    }
    return n;
}

static int conectar(const wchar_t *adb, const char *dir)
{
    wchar_t cmd[MAX_PATH + 96];
    DWORD codigo;
    int ok;

    swprintf(cmd, MAX_PATH + 96, L"\"%ls\" connect %hs", adb, dir);
    if (!ejecutar_capturando(cmd, s_salida, sizeof s_salida, 8000, &codigo))
        return 0;
    /* "connected to ..." o "already connected to ..."; si falla dice "cannot/failed to connect" */
    ok = strstr(s_salida, "connected to") != NULL;
    log_adb(L"adb connect", s_salida);
    return ok;
}

/* Direcciones que rechazaron la conexion hace poco: el celular sigue anunciando la vieja un rato
   despues de apagar la Depuracion inalambrica, y no tiene sentido probarla cada 10 s. */
#define MAX_FALLIDOS 8
#define FALLO_RECIENTE_MS 60000
static struct {
    char  dir[64];
    DWORD cuando;
} s_fallidos[MAX_FALLIDOS];

static int fallo_reciente(const char *dir)
{
    int i;
    for (i = 0; i < MAX_FALLIDOS; i++)
        if (!strcmp(s_fallidos[i].dir, dir) && GetTickCount() - s_fallidos[i].cuando < FALLO_RECIENTE_MS)
            return 1;
    return 0;
}

static void anotar_fallo(const char *dir)
{
    int i, elegido = 0;
    for (i = 0; i < MAX_FALLIDOS; i++) {
        if (!strcmp(s_fallidos[i].dir, dir)) {
            elegido = i;
            break;
        }
        if (s_fallidos[i].cuando < s_fallidos[elegido].cuando)
            elegido = i;
    }
    strcpy(s_fallidos[elegido].dir, dir);
    s_fallidos[elegido].cuando = GetTickCount();
}

int dispositivo_conectar_wifi(const wchar_t *adb, int forzar)
{
    ServicioMdns s[16];
    int n = mdns_listar(adb, s, 16), i;

    for (i = 0; i < n; i++) {
        if (!strstr(s[i].tipo, "_adb-tls-connect") || (!forzar && fallo_reciente(s[i].dir)))
            continue;
        if (conectar(adb, s[i].dir))
            return 1;
        anotar_fallo(s[i].dir);
    }
    return 0;
}

int dispositivo_emparejar(const wchar_t *adb, const char *nombre, const char *clave, AvisoFn aviso)
{
    ServicioMdns s[16];
    wchar_t cmd[MAX_PATH + 128];
    char ip[64], *dos_puntos;
    DWORD codigo, inicio;
    int n, i;

    /* al escanear el QR, el celular se anuncia con el nombre que lleva el QR */
    n = mdns_listar(adb, s, 16);
    for (i = 0; i < n; i++)
        if (strstr(s[i].tipo, "_adb-tls-pairing") && !strcmp(s[i].nombre, nombre))
            break;
    if (i == n)
        return 0;

    aviso(L"Celular encontrado: emparejando...");
    log_escribir(L"Emparejando con %hs", s[i].dir);
    swprintf(cmd, MAX_PATH + 128, L"\"%ls\" pair %hs %hs", adb, s[i].dir, clave);
    if (!ejecutar_capturando(cmd, s_salida, sizeof s_salida, 20000, &codigo) ||
        !strstr(s_salida, "Successfully paired")) {
        log_adb(L"adb pair", s_salida);
        return -1;
    }
    log_adb(L"adb pair", s_salida);

    /* la conexion usa otro puerto, que el celular anuncia aparte con la misma IP */
    strcpy(ip, s[i].dir);
    dos_puntos = strrchr(ip, ':');
    if (dos_puntos)
        dos_puntos[1] = 0; /* "192.168.1.5:" para comparar el principio */
    aviso(L"Emparejado. Conectando...");
    for (inicio = GetTickCount(); GetTickCount() - inicio < 15000; Sleep(1000)) {
        n = mdns_listar(adb, s, 16);
        for (i = 0; i < n; i++)
            if (strstr(s[i].tipo, "_adb-tls-connect") && !strncmp(s[i].dir, ip, strlen(ip)) &&
                conectar(adb, s[i].dir))
                return 1;
    }
    /* quedo emparejado: dispositivo_conectar_wifi lo sigue intentando */
    log_escribir(L"Emparejado, pero el celular todavía no aceptó la conexión");
    return 1;
}

void dispositivo_describir(const Dispositivo *d, wchar_t *dest, size_t cap)
{
    switch (d->estado) {
    case DISP_LISTO:
        swprintf(dest, cap, L"%ls  ·  Android %ls  ·  %ls",
                 d->modelo[0] ? d->modelo : d->serial, d->android[0] ? d->android : L"?",
                 d->es_usb ? L"USB" : L"WiFi");
        break;
    case DISP_SIN_PERMISO:
        swprintf(dest, cap, L"Aceptá el permiso \"¿Permitir depuración USB?\" en el celular");
        break;
    case DISP_CONECTANDO:
        swprintf(dest, cap, L"Conectando con el celular...");
        break;
    case DISP_OFFLINE:
        swprintf(dest, cap, L"El celular no responde: desenchufá y volvé a enchufar el cable");
        break;
    case DISP_SOLO_OTRA_CONEXION:
        swprintf(dest, cap, L"Hay un celular, pero por otra conexión (revisá 'conexion' en la configuración)");
        break;
    default:
        swprintf(dest, cap, L"Conectá el celular por USB, o por WiFi con el código QR");
        break;
    }
}
