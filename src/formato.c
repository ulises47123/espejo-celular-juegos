/*
 * Formato del monitor: mientras se juega, la resolucion del celular se pasa a la misma
 * proporcion que el monitor (por ejemplo 16:9) para que la imagen llene toda la pantalla sin
 * deformarse ni recortarse. Al terminar se devuelve la original.
 *
 * Lo original se guarda en restaurar.ini ANTES de cambiar nada: si el programa o la PC se
 * cierran de golpe, se restaura la proxima vez que se detecte ese celular.
 */
#include "rtxjuegos.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct {
    int fis_w, fis_h;     /* "Physical size" */
    int ovr_w, ovr_h;     /* "Override size" (0 si no hay) */
    int fis_dpi, ovr_dpi; /* "Physical density" / "Override density" */
} Pantalla;

static char s_salida[4096];

static void ruta_restaurar(wchar_t *dest)
{
    ruta_unir(dest, MAX_PATH, g_rutas.base, L"restaurar.ini");
}

static int shell(const wchar_t *adb, const wchar_t *serial, const wchar_t *args)
{
    wchar_t cmd[MAX_PATH + 512];
    DWORD codigo;
    swprintf(cmd, MAX_PATH + 512, L"\"%ls\" -s \"%ls\" shell %ls", adb, serial, args);
    if (!ejecutar_capturando(cmd, s_salida, sizeof s_salida, 10000, &codigo))
        return 0;
    log_escribir(L"adb shell %ls -> %lu", args, codigo);
    return codigo == 0;
}

static int leer_pantalla(const wchar_t *adb, const wchar_t *serial, Pantalla *p)
{
    const char *s;
    ZeroMemory(p, sizeof *p);
    if (!shell(adb, serial, L"wm size"))
        return 0;
    if ((s = strstr(s_salida, "Physical size:")))
        sscanf(s, "Physical size: %dx%d", &p->fis_w, &p->fis_h);
    if ((s = strstr(s_salida, "Override size:")))
        sscanf(s, "Override size: %dx%d", &p->ovr_w, &p->ovr_h);
    if (shell(adb, serial, L"wm density")) {
        if ((s = strstr(s_salida, "Physical density:")))
            sscanf(s, "Physical density: %d", &p->fis_dpi);
        if ((s = strstr(s_salida, "Override density:")))
            sscanf(s, "Override density: %d", &p->ovr_dpi);
    }
    return p->fis_w > 0 && p->fis_h > 0;
}

static int redondear8(double v)
{
    int n = (int)(v / 8.0 + 0.5) * 8;
    return n < 8 ? 8 : n;
}

int formato_aplicar(const wchar_t *adb, const Dispositivo *d, int mon_w, int mon_h,
                    wchar_t *info, size_t cap)
{
    const wchar_t *serial = d->serial;
    Pantalla p;
    wchar_t restaurar[MAX_PATH], previo[128], args[64], valor[32];
    int w, h, dpi, corto, largo, obj_corto, obj_largo, obj_w, obj_h, obj_dpi;
    double prop_mon, prop_cel;

    info[0] = 0;
    if (mon_w <= 0 || mon_h <= 0 || !leer_pantalla(adb, serial, &p)) {
        swprintf(info, cap, L"No se pudo leer la resolución del celular; se juega con la actual.");
        return 0;
    }

    /* resolucion efectiva actual (la que eligio el usuario, si cambio alguna) */
    w = p.ovr_w ? p.ovr_w : p.fis_w;
    h = p.ovr_h ? p.ovr_h : p.fis_h;
    dpi = p.ovr_dpi ? p.ovr_dpi : p.fis_dpi;
    corto = w < h ? w : h;
    largo = w < h ? h : w;
    prop_mon = (double)(mon_w > mon_h ? mon_w : mon_h) / (mon_w > mon_h ? mon_h : mon_w);
    prop_cel = (double)largo / corto;

    if (prop_cel / prop_mon > 0.99 && prop_cel / prop_mon < 1.01) {
        swprintf(info, cap, L"El celular ya tiene el formato del monitor (%dx%d).", w, h);
        return 0;
    }
    if (prop_cel > prop_mon) {
        /* celular mas alargado que el monitor (lo comun: 20:9 vs 16:9): se acorta el lado largo */
        obj_corto = redondear8(corto);
        obj_largo = redondear8(corto * prop_mon);
    } else {
        /* celular mas "cuadrado" (tablets) o monitor ultra ancho: se achica el lado corto */
        obj_largo = redondear8(largo);
        obj_corto = redondear8(largo / prop_mon);
    }
    /* se respeta la orientacion natural del aparato */
    obj_w = w < h ? obj_corto : obj_largo;
    obj_h = w < h ? obj_largo : obj_corto;
    /* si cambia el lado corto, la densidad acompaña para que los menus no cambien de tamaño */
    obj_dpi = (obj_corto != corto && dpi > 0) ? (int)((double)dpi * obj_corto / corto + 0.5) : 0;

    /* guardar lo original, salvo que ya haya algo pendiente de este celular (no pisar el original) */
    ruta_restaurar(restaurar);
    GetPrivateProfileStringW(L"celular", L"id", L"", previo, 128, restaurar);
    if (wcscmp(previo, d->id)) {
        WritePrivateProfileStringW(L"celular", L"id", d->id, restaurar);
        WritePrivateProfileStringW(L"celular", L"modelo", d->modelo, restaurar);
        if (p.ovr_w)
            swprintf(valor, 32, L"%dx%d", p.ovr_w, p.ovr_h);
        else
            wcscpy(valor, L"reset");
        WritePrivateProfileStringW(L"celular", L"size", valor, restaurar);
        if (obj_dpi) {
            if (p.ovr_dpi)
                swprintf(valor, 32, L"%d", p.ovr_dpi);
            else
                wcscpy(valor, L"reset");
            WritePrivateProfileStringW(L"celular", L"density", valor, restaurar);
        }
        WritePrivateProfileStringW(NULL, NULL, NULL, restaurar); /* forzar escritura a disco */
    }

    swprintf(args, 64, L"wm size %dx%d", obj_w, obj_h);
    if (!shell(adb, serial, args)) {
        swprintf(info, cap, L"El celular no permitió cambiar la resolución; se juega con la actual.");
        formato_restaurar_pendiente(adb, d);
        return 0;
    }
    if (obj_dpi) {
        swprintf(args, 64, L"wm density %d", obj_dpi);
        shell(adb, serial, args);
    }
    swprintf(info, cap, L"Celular en formato del monitor: %dx%d → %dx%d. "
                        L"Si el juego ya estaba abierto y no llena la pantalla, cerralo y abrilo de nuevo.",
             w, h, obj_w, obj_h);
    return 1;
}

void formato_restaurar_pendiente(const wchar_t *adb, const Dispositivo *d)
{
    const wchar_t *serial = d->serial;
    wchar_t restaurar[MAX_PATH], guardado[128], valor[32], args[64];
    int ok;

    ruta_restaurar(restaurar);
    if (!archivo_existe(restaurar))
        return;
    GetPrivateProfileStringW(L"celular", L"id", L"", guardado, 128, restaurar);
    if (wcscmp(guardado, d->id))
        return; /* es de otro celular: se restaura cuando se conecte ese */

    GetPrivateProfileStringW(L"celular", L"size", L"", valor, 32, restaurar);
    swprintf(args, 64, L"wm size %ls", valor[0] ? valor : L"reset");
    ok = shell(adb, serial, args);

    GetPrivateProfileStringW(L"celular", L"density", L"", valor, 32, restaurar);
    if (valor[0]) {
        swprintf(args, 64, L"wm density %ls", valor);
        ok = shell(adb, serial, args) && ok;
    }
    if (ok) {
        DeleteFileW(restaurar);
        log_escribir(L"Resolución original del celular restaurada");
    }
}
