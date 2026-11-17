/*
 * Ventana con el codigo QR para conectar el celular por WiFi ("Depuracion inalambrica").
 *
 * El QR lleva "WIFI:T:ADB;S:<nombre>;P:<clave>;;". Al escanearlo, el celular se anuncia en la
 * red con ese nombre y el hilo de trabajo lo empareja con "adb pair" (ver dispositivo.c).
 * El dibujo del QR lo hace qrcodegen.c (Project Nayuki, licencia MIT).
 */
#include "rtxjuegos.h"
#include "qrcodegen.h"
#include <bcrypt.h>
#include <stdio.h>
#include <wchar.h>

#define QR_VERSION_MAX 10   /* el texto entra holgado en una version 4 */

volatile LONG g_qr_activo;

static HWND s_qr;
static HFONT s_fuente;
static uint8_t s_codigo[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];
static int s_codigo_ok;
static wchar_t s_estado[256];

static const wchar_t INSTRUCCIONES[] =
    L"En el celular:\n"
    L"1. Opciones de desarrollador → Depuración inalámbrica: activala.\n"
    L"2. Tocá \"Vincular dispositivo con código QR\" y escaneá este código.\n\n"
    L"El celular y la PC tienen que estar conectados a la misma red.";

static int S(int v)
{
    static int dpi = 0;
    if (!dpi) {
        HDC dc = GetDC(NULL);
        dpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(NULL, dc);
    }
    return MulDiv(v, dpi, 96);
}

static void texto_azar(char *dest, size_t largo)
{
    /* sin letras que se confundan (l, o, 0, 1); 32 simbolos: el modulo no sesga */
    static const char simbolos[] = "abcdefghijkmnpqrstuvwxyz23456789";
    unsigned char azar[32];
    size_t i;
    if (largo > sizeof azar)
        largo = sizeof azar;
    if (BCryptGenRandom(NULL, azar, (ULONG)largo, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        for (i = 0; i < largo; i++)
            azar[i] = (unsigned char)(GetTickCount() * 31 + i * 17);
    for (i = 0; i < largo; i++)
        dest[i] = simbolos[azar[i] % 32];
    dest[largo] = 0;
}

void qr_nuevos_datos(char *nombre, size_t cap_nombre, char *clave, size_t cap_clave)
{
    char azar[16];
    texto_azar(azar, 6);
    _snprintf(nombre, cap_nombre, "rtxjuegos-%s", azar);
    nombre[cap_nombre - 1] = 0;
    texto_azar(clave, cap_clave - 1 < 10 ? cap_clave - 1 : 10);
}

static void pintar(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cli, r;
    int lado = S(260), x0, y0 = S(14);

    GetClientRect(h, &cli);
    x0 = (cli.right - lado) / 2;
    SetRect(&r, x0, y0, x0 + lado, y0 + lado);
    FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));

    if (s_codigo_ok) {
        int n = qrcodegen_getSize(s_codigo), x, y;
        int modulo = lado / (n + 8);   /* 4 modulos de margen blanco por lado */
        int ox = x0 + (lado - modulo * n) / 2, oy = y0 + (lado - modulo * n) / 2;
        HBRUSH negro = (HBRUSH)GetStockObject(BLACK_BRUSH);
        for (y = 0; y < n; y++)
            for (x = 0; x < n; x++)
                if (qrcodegen_getModule(s_codigo, x, y)) {
                    SetRect(&r, ox + x * modulo, oy + y * modulo, ox + (x + 1) * modulo, oy + (y + 1) * modulo);
                    FillRect(dc, &r, negro);
                }
    }

    SelectObject(dc, s_fuente);
    SetBkMode(dc, TRANSPARENT);
    SetRect(&r, S(20), y0 + lado + S(12), cli.right - S(20), y0 + lado + S(122));
    DrawTextW(dc, INSTRUCCIONES, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
    SetTextColor(dc, RGB(0, 90, 180));
    SetRect(&r, S(20), y0 + lado + S(126), cli.right - S(20), cli.bottom - S(8));
    DrawTextW(dc, s_estado, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK proc_qr(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        pintar(h);
        return 0;
    case WM_DESTROY:
        /* cerrada a mano o porque ya se conecto: el hilo de trabajo deja de buscar el emparejamiento */
        s_qr = NULL;
        InterlockedExchange(&g_qr_activo, 0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void qr_mostrar(HWND dueno, HFONT fuente, const char *texto)
{
    static uint8_t temporal[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION_MAX)];

    if (!texto && !s_qr)
        return;
    s_fuente = fuente;
    if (texto) {
        s_codigo_ok = qrcodegen_encodeText(texto, temporal, s_codigo, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                           QR_VERSION_MAX, qrcodegen_Mask_AUTO, true);
        wcscpy(s_estado, L"Esperando que escanees el código...");
    }

    if (!s_qr) {
        static int registrada = 0;
        DWORD estilo = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        RECT r = { 0, 0, S(340), S(470) }, rd;
        int w, alto, x, y;
        MONITORINFO mi = { sizeof mi };
        if (!registrada) {
            WNDCLASSEXW wc = { sizeof wc };
            wc.lpfnWndProc = proc_qr;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            wc.lpszClassName = L"RTXJuegosQR";
            RegisterClassExW(&wc);
            registrada = 1;
        }
        AdjustWindowRect(&r, estilo, FALSE);
        w = r.right - r.left;
        alto = r.bottom - r.top;
        /* al lado de la ventana principal, para que se vean las dos, sin salirse de la pantalla */
        GetWindowRect(dueno, &rd);
        GetMonitorInfoW(MonitorFromWindow(dueno, MONITOR_DEFAULTTONEAREST), &mi);
        x = rd.right + S(8);
        if (x + w > mi.rcWork.right)
            x = rd.left - S(8) - w;
        if (x < mi.rcWork.left)
            x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
        y = rd.top;
        if (y + alto > mi.rcWork.bottom)
            y = mi.rcWork.bottom - alto;
        if (y < mi.rcWork.top)
            y = mi.rcWork.top;
        s_qr = CreateWindowExW(0, L"RTXJuegosQR", L"Conectar por WiFi", estilo, x, y, w, alto, dueno, NULL,
                               GetModuleHandleW(NULL), NULL);
        if (!s_qr)
            return;
    } else {
        InvalidateRect(s_qr, NULL, TRUE);
    }
    ShowWindow(s_qr, SW_SHOWNORMAL);
    SetForegroundWindow(s_qr);
}

void qr_estado(const wchar_t *texto)
{
    wcsncpy(s_estado, texto, 255);
    s_estado[255] = 0;
    if (s_qr)
        InvalidateRect(s_qr, NULL, TRUE);
}

void qr_cerrar(void)
{
    if (s_qr)
        DestroyWindow(s_qr);
}
