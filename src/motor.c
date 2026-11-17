/*
 * Motor: scrcpy + adb en la carpeta "motor". Si falta (o la version no coincide con la
 * configurada) se descarga de GitHub, se verifica el SHA256 y se descomprime.
 */
#include "rtxjuegos.h"
#include <wininet.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define URL_BASE L"https://github.com/Genymobile/scrcpy/releases/download/v%ls/"

void motor_ruta_scrcpy(wchar_t *dest, size_t cap)
{
    ruta_unir(dest, cap, g_rutas.motor, L"scrcpy.exe");
}

void motor_ruta_adb(const ConfigGeneral *g, wchar_t *dest, size_t cap)
{
    if (!_wcsicmp(g->adb, L"sistema")) {
        wchar_t *parte;
        if (SearchPathW(NULL, L"adb.exe", NULL, (DWORD)cap, dest, &parte))
            return;
        log_escribir(L"adb=sistema pero no hay adb.exe en el PATH; se usa el del motor");
    } else if (g->adb[0] && _wcsicmp(g->adb, L"motor") && archivo_existe(g->adb)) {
        wcsncpy(dest, g->adb, cap - 1);
        dest[cap - 1] = 0;
        return;
    }
    ruta_unir(dest, cap, g_rutas.motor, L"adb.exe");
}

static void leer_version_instalada(wchar_t *dest, size_t cap)
{
    wchar_t ruta[MAX_PATH];
    char buf[64] = { 0 };
    DWORD leido = 0;
    HANDLE h;
    char *p;

    dest[0] = 0;
    ruta_unir(ruta, MAX_PATH, g_rutas.motor, L"version.txt");
    h = CreateFileW(ruta, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    ReadFile(h, buf, sizeof buf - 1, &leido, NULL);
    CloseHandle(h);
    buf[leido] = 0;
    for (p = buf; *p; p++)
        if (*p == '\r' || *p == '\n' || *p == ' ')
            *p = 0;
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, dest, (int)cap);
}

int motor_listo(const wchar_t *version)
{
    static const wchar_t *necesarios[] = { L"scrcpy.exe", L"adb.exe", L"scrcpy-server", L"SDL3.dll" };
    wchar_t ruta[MAX_PATH], instalada[32];
    size_t i;

    for (i = 0; i < sizeof necesarios / sizeof *necesarios; i++) {
        ruta_unir(ruta, MAX_PATH, g_rutas.motor, necesarios[i]);
        if (!archivo_existe(ruta))
            return 0;
    }
    leer_version_instalada(instalada, 32);
    /* Sin version.txt (motor copiado a mano) se acepta tal cual */
    return !instalada[0] || !_wcsicmp(instalada, version);
}

/* ---------- Descarga con WinINet + SHA256 con BCrypt ---------- */

typedef struct {
    BCRYPT_ALG_HANDLE  alg;
    BCRYPT_HASH_HANDLE hash;
} Sha256;

static int sha_iniciar(Sha256 *s)
{
    s->alg = NULL;
    s->hash = NULL;
    if (BCryptOpenAlgorithmProvider(&s->alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
        return 0;
    return BCryptCreateHash(s->alg, &s->hash, NULL, 0, NULL, 0, 0) >= 0;
}

static void sha_terminar(Sha256 *s, char hex[65])
{
    unsigned char dig[32];
    int i;
    hex[0] = 0;
    if (s->hash && BCryptFinishHash(s->hash, dig, 32, 0) >= 0) {
        for (i = 0; i < 32; i++)
            sprintf(hex + i * 2, "%02x", dig[i]);
    }
    if (s->hash)
        BCryptDestroyHash(s->hash);
    if (s->alg)
        BCryptCloseAlgorithmProvider(s->alg, 0);
}

/* Descarga 'url' a 'archivo' (o a memoria si archivo == NULL). Calcula SHA256 si sha != NULL. */
static int descargar(const wchar_t *url, const wchar_t *archivo, char *memoria, size_t cap_mem,
                     Sha256 *sha, ProgresoFn progreso, volatile LONG *cancelar,
                     wchar_t *error, size_t cap_error)
{
    HINTERNET red, con;
    HANDLE salida = INVALID_HANDLE_VALUE;
    DWORD estado = 0, total = 0, tam = sizeof(DWORD), leido, escrito;
    unsigned long long recibido = 0;
    static char buf[65536];
    int ok = 0, ultimo = -1;

    red = InternetOpenW(L"RTXJuegos/" RTX_VERSION, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!red) {
        swprintf(error, cap_error, L"No se pudo iniciar la conexión a Internet (error %lu).", GetLastError());
        return 0;
    }
    con = InternetOpenUrlW(red, url, NULL, 0,
                           INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE |
                           INTERNET_FLAG_NO_UI, 0);
    if (!con) {
        swprintf(error, cap_error, L"No se pudo conectar a GitHub (error %lu). ¿Hay Internet?", GetLastError());
        InternetCloseHandle(red);
        return 0;
    }
    HttpQueryInfoW(con, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &estado, &tam, NULL);
    if (estado != 200) {
        swprintf(error, cap_error, L"GitHub respondió %lu para:\n%ls", estado, url);
        goto fin;
    }
    tam = sizeof(DWORD);
    HttpQueryInfoW(con, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &total, &tam, NULL);

    if (archivo) {
        salida = CreateFileW(archivo, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (salida == INVALID_HANDLE_VALUE) {
            swprintf(error, cap_error, L"No se pudo crear %ls (error %lu).", archivo, GetLastError());
            goto fin;
        }
    }

    for (;;) {
        if (cancelar && *cancelar) {
            swprintf(error, cap_error, L"Descarga cancelada.");
            goto fin;
        }
        if (!InternetReadFile(con, buf, sizeof buf, &leido)) {
            swprintf(error, cap_error, L"Se cortó la descarga (error %lu).", GetLastError());
            goto fin;
        }
        if (leido == 0)
            break;
        if (sha)
            BCryptHashData(sha->hash, (PUCHAR)buf, leido, 0);
        if (archivo) {
            if (!WriteFile(salida, buf, leido, &escrito, NULL) || escrito != leido) {
                swprintf(error, cap_error, L"No se pudo escribir en el disco (¿falta espacio?).");
                goto fin;
            }
        } else if (memoria) {
            size_t copiar = recibido + leido < cap_mem - 1 ? leido : (size_t)(cap_mem - 1 - recibido);
            memcpy(memoria + recibido, buf, copiar);
            memoria[recibido + copiar] = 0;
            if (copiar < leido)
                break;
        }
        recibido += leido;
        if (progreso && total) {
            int pct = (int)(recibido * 100 / total);
            if (pct != ultimo) {
                wchar_t txt[96];
                swprintf(txt, 96, L"Descargando scrcpy + adb... %.1f / %.1f MB",
                         recibido / 1048576.0, total / 1048576.0);
                progreso(pct, txt);
                ultimo = pct;
            }
        }
    }
    ok = 1;

fin:
    if (salida != INVALID_HANDLE_VALUE)
        CloseHandle(salida);
    InternetCloseHandle(con);
    InternetCloseHandle(red);
    return ok;
}

/* Busca en SHA256SUMS.txt la linea del archivo y devuelve su hash en minusculas */
static int buscar_hash(const char *sums, const char *nombre, char hex[65])
{
    const char *linea = sums;
    while (linea && *linea) {
        const char *fin = strchr(linea, '\n');
        size_t len = fin ? (size_t)(fin - linea) : strlen(linea);
        if (len > 64 && strstr(linea, nombre) && (size_t)(strstr(linea, nombre) - linea) < len) {
            int i;
            for (i = 0; i < 64; i++) {
                char c = linea[i];
                hex[i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
            }
            hex[64] = 0;
            return 1;
        }
        linea = fin ? fin + 1 : NULL;
    }
    return 0;
}

static int descomprimir(const wchar_t *zip, const wchar_t *destino)
{
    wchar_t cmd[2048], sistema[MAX_PATH], tar[MAX_PATH];
    DWORD codigo;
    char salida[2048];

    /* tar.exe viene con Windows 10 1803+; si no esta, PowerShell */
    GetSystemDirectoryW(sistema, MAX_PATH);
    ruta_unir(tar, MAX_PATH, sistema, L"tar.exe");
    if (archivo_existe(tar)) {
        swprintf(cmd, 2048, L"\"%ls\" -xf \"%ls\" -C \"%ls\"", tar, zip, destino);
        if (ejecutar_capturando(cmd, salida, sizeof salida, 120000, &codigo) && codigo == 0)
            return 1;
        log_escribir(L"tar falló (%lu), se prueba con PowerShell", codigo);
    }
    swprintf(cmd, 2048,
             L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
             L"\"Expand-Archive -LiteralPath '%ls' -DestinationPath '%ls' -Force\"",
             zip, destino);
    return ejecutar_capturando(cmd, salida, sizeof salida, 300000, &codigo) && codigo == 0;
}

int motor_descargar(const wchar_t *version, ProgresoFn progreso, volatile LONG *cancelar,
                    wchar_t *error, size_t cap_error)
{
    wchar_t url[512], zip[MAX_PATH], tmp[MAX_PATH], interna[MAX_PATH], viejo[MAX_PATH], vtxt[MAX_PATH];
    wchar_t nombre_w[128];
    char nombre[128], esperado[65] = { 0 }, obtenido[65];
    static char sums[16384];
    wchar_t err_sums[256];
    Sha256 sha;
    HANDLE h;
    DWORD escrito;
    char vutf8[64];

    swprintf(nombre_w, 128, L"scrcpy-win64-v%ls", version);
    WideCharToMultiByte(CP_UTF8, 0, nombre_w, -1, nombre, sizeof nombre, NULL, NULL);
    strcat(nombre, ".zip");

    /* 1) hash oficial publicado en la release */
    if (progreso)
        progreso(0, L"Consultando la versión de scrcpy en GitHub...");
    swprintf(url, 512, URL_BASE L"SHA256SUMS.txt", version);
    if (descargar(url, NULL, sums, sizeof sums, NULL, NULL, cancelar, err_sums, 256))
        buscar_hash(sums, nombre, esperado);
    if (!esperado[0] && !_wcsicmp(version, RTX_SCRCPY_DEF))
        strcpy(esperado, RTX_SHA256_DEF);
    if (!esperado[0]) {
        swprintf(error, cap_error,
                 L"No se encontró el hash de seguridad de scrcpy v%ls.\n"
                 L"Revisá 'version_scrcpy' en rtxjuegos.ini o la conexión a Internet.", version);
        return 0;
    }

    /* 2) descargar el zip calculando el SHA256 en el camino */
    ruta_unir(zip, MAX_PATH, g_rutas.base, L"motor-descarga.zip");
    swprintf(url, 512, URL_BASE L"%ls.zip", version, nombre_w);
    log_escribir(L"Descargando %ls", url);
    if (!sha_iniciar(&sha)) {
        swprintf(error, cap_error, L"No se pudo iniciar SHA256.");
        return 0;
    }
    if (!descargar(url, zip, NULL, 0, &sha, progreso, cancelar, error, cap_error)) {
        sha_terminar(&sha, obtenido);
        DeleteFileW(zip);
        return 0;
    }
    sha_terminar(&sha, obtenido);
    if (strcmp(obtenido, esperado) != 0) {
        swprintf(error, cap_error, L"El archivo descargado está dañado o fue alterado (SHA256 no coincide).\n"
                                   L"Se borró por seguridad. Probá de nuevo.");
        log_escribir(L"SHA256 esperado %hs, obtenido %hs", esperado, obtenido);
        DeleteFileW(zip);
        return 0;
    }
    log_escribir(L"SHA256 verificado: %hs", obtenido);

    /* 3) descomprimir en una carpeta temporal y reemplazar el motor */
    if (progreso)
        progreso(100, L"Descomprimiendo...");
    ruta_unir(tmp, MAX_PATH, g_rutas.base, L"motor-tmp");
    if (carpeta_existe(tmp))
        borrar_carpeta(tmp);
    CreateDirectoryW(tmp, NULL);
    if (!descomprimir(zip, tmp)) {
        swprintf(error, cap_error, L"No se pudo descomprimir scrcpy.");
        DeleteFileW(zip);
        borrar_carpeta(tmp);
        return 0;
    }
    DeleteFileW(zip);

    ruta_unir(interna, MAX_PATH, tmp, nombre_w);
    if (!carpeta_existe(interna))
        wcscpy(interna, tmp); /* zip sin carpeta interna */

    if (carpeta_existe(g_rutas.motor)) {
        /* el servidor adb del motor viejo bloquea adb.exe */
        wchar_t adb_viejo[MAX_PATH], cmd[MAX_PATH + 32];
        DWORD codigo;
        char salida[512];
        ruta_unir(adb_viejo, MAX_PATH, g_rutas.motor, L"adb.exe");
        if (archivo_existe(adb_viejo)) {
            swprintf(cmd, MAX_PATH + 32, L"\"%ls\" kill-server", adb_viejo);
            ejecutar_capturando(cmd, salida, sizeof salida, 10000, &codigo);
            Sleep(500);
        }
        ruta_unir(viejo, MAX_PATH, g_rutas.base, L"motor-viejo");
        if (carpeta_existe(viejo))
            borrar_carpeta(viejo);
        if (!MoveFileExW(g_rutas.motor, viejo, 0)) {
            swprintf(error, cap_error, L"No se pudo reemplazar la carpeta motor (¿scrcpy o adb está abierto?).");
            borrar_carpeta(tmp);
            return 0;
        }
        borrar_carpeta(viejo);
    }
    if (!MoveFileExW(interna, g_rutas.motor, 0)) {
        swprintf(error, cap_error, L"No se pudo crear la carpeta motor (error %lu).", GetLastError());
        borrar_carpeta(tmp);
        return 0;
    }
    if (carpeta_existe(tmp))
        borrar_carpeta(tmp);

    ruta_unir(vtxt, MAX_PATH, g_rutas.motor, L"version.txt");
    h = CreateFileW(vtxt, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WideCharToMultiByte(CP_UTF8, 0, version, -1, vutf8, sizeof vutf8, NULL, NULL);
        WriteFile(h, vutf8, (DWORD)strlen(vutf8), &escrito, NULL);
        CloseHandle(h);
    }
    log_escribir(L"scrcpy v%ls instalado en %ls", version, g_rutas.motor);
    return 1;
}
