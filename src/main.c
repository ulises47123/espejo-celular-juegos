/*
 * RTXJuegos - ventana principal y hilo de trabajo.
 *
 * La ventana muestra el estado del celular y el perfil elegido. Todo lo lento (descargar el
 * motor, consultar adb, ejecutar scrcpy) corre en un hilo aparte que avisa con mensajes WM_RTX_*.
 */
#include "rtxjuegos.h"
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

Rutas g_rutas;

enum {
    ID_PERFIL = 100,
    ID_EDITAR,
    ID_JUGAR,
    ID_QR,
    ID_REGISTRO,
    ID_SALIR
};

static HWND s_ventana, s_lbl_celular, s_combo, s_progreso, s_estado, s_btn_jugar, s_btn_qr;
static HFONT s_fuente, s_fuente_titulo;
static int s_celular_listo;
static Modo s_modo = MODO_OCUPADO;
static int s_cerrar_al_terminar;
/* Al reaparecer la ventana, el boton Jugar tiene el foco: un Espacio/Enter que venia
   tecleandose (o la repeticion de tecla) lo "tocaria" sin querer. Se ignora un instante. */
#define ESPERA_ANTI_REBOTE_MS 1000
static DWORD s_mostrada_en;

static HANDLE s_hilo;
static HANDLE s_evento_jugar;          /* lo dispara el boton Jugar (o Reintentar) */
static HANDLE s_evento_qr;             /* lo dispara el boton "Conectar por WiFi (QR)" */
static volatile LONG s_salir;
static volatile LONG s_cancelar;       /* cancela la espera de reconexion */
/* datos del QR que se esta mostrando (solo los usa el hilo de trabajo) */
static char s_qr_nombre[32], s_qr_clave[32];

/* ---------- avisos del hilo a la ventana ---------- */

static void enviar_texto(UINT msg, WPARAM wp, const wchar_t *texto)
{
    wchar_t *copia = _wcsdup(texto ? texto : L"");
    if (!PostMessageW(s_ventana, msg, wp, (LPARAM)copia))
        free(copia);
}

static void estado(const wchar_t *texto)
{
    log_escribir(L"%ls", texto);
    enviar_texto(WM_RTX_ESTADO, 0, texto);
}

static void cambiar_modo(Modo m, DWORD codigo)
{
    PostMessageW(s_ventana, WM_RTX_MODO, (WPARAM)m, (LPARAM)codigo);
}

static void progreso_descarga(int pct, const wchar_t *texto)
{
    PostMessageW(s_ventana, WM_RTX_PROGRESO, (WPARAM)pct, 0);
    enviar_texto(WM_RTX_ESTADO, 0, texto);
}

/* ---------- hilo de trabajo ---------- */

static void aviso_qr(const wchar_t *texto)
{
    log_escribir(L"QR: %ls", texto);
    enviar_texto(WM_RTX_QR_ESTADO, 0, texto);
}

/* Muestra el QR para emparejar por WiFi; si ya esta abierto lo trae al frente (o, con 'nuevo',
   lo cambia por otro: un QR que fallo al emparejar no se vuelve a usar) */
static void abrir_qr(int nuevo)
{
    char texto[128], *copia;
    if (g_qr_activo && !nuevo) {
        PostMessageW(s_ventana, WM_RTX_QR, 1, 0);
        return;
    }
    qr_nuevos_datos(s_qr_nombre, sizeof s_qr_nombre, s_qr_clave, sizeof s_qr_clave);
    _snprintf(texto, sizeof texto, "WIFI:T:ADB;S:%s;P:%s;;", s_qr_nombre, s_qr_clave);
    texto[sizeof texto - 1] = 0;
    InterlockedExchange(&g_qr_activo, 1);
    log_escribir(L"Código QR para emparejar por WiFi (%hs)", s_qr_nombre);
    copia = _strdup(texto);
    if (!PostMessageW(s_ventana, WM_RTX_QR, 1, (LPARAM)copia))
        free(copia);
}

static void cerrar_qr(void)
{
    InterlockedExchange(&g_qr_activo, 0);
    PostMessageW(s_ventana, WM_RTX_QR, 0, 0);
}

static void emparejar_qr(const ConfigGeneral *g, const wchar_t *adb, Dispositivo *d)
{
    int r = dispositivo_emparejar(adb, s_qr_nombre, s_qr_clave, aviso_qr);
    if (r < 0) {
        abrir_qr(1);
        aviso_qr(L"No se pudo emparejar. Escaneá de nuevo este código (es uno nuevo).");
    } else if (r > 0) {
        cerrar_qr();
        dispositivo_buscar(adb, g->conexion, d);
    }
}

typedef struct {
    DWORD sin_celular_desde;
    DWORD ultimo_wifi, ultimo_reinicio, ultimo_reconectar;
    int   qr_ofrecido;     /* el QR se abre solo una vez cada vez que falta el celular */
    int   cambio_estado;   /* se dejo un aviso en la ventana: al aparecer el celular se repone "Listo" */
} Busqueda;

/* Sin celular listo: hace solo lo que antes habia que hacer a mano con adb desde el CMD */
static void buscar_de_otra_forma(const ConfigGeneral *g, const wchar_t *adb, Dispositivo *d, Busqueda *b)
{
    DWORD ahora = GetTickCount(), sin = ahora - b->sin_celular_desde;

    if (d->estado == DISP_OFFLINE && ahora - b->ultimo_reconectar > 10000) {
        b->ultimo_reconectar = ahora;
        dispositivo_reconectar_offline(adb);
        dispositivo_buscar(adb, g->conexion, d);
    }
    /* celulares ya emparejados por WiFi */
    if (d->estado != DISP_LISTO && g->conexion != CONEXION_USB && ahora - b->ultimo_wifi > 10000) {
        b->ultimo_wifi = ahora;
        if (dispositivo_conectar_wifi(adb, 0))
            dispositivo_buscar(adb, g->conexion, d);
    }
    /* adb a veces no ve un celular recien enchufado hasta que se reinicia */
    if (d->estado == DISP_NINGUNO && g->conexion != CONEXION_WIFI && sin > 6000 &&
        (!b->ultimo_reinicio || GetTickCount() - b->ultimo_reinicio > 60000)) {
        b->ultimo_reinicio = GetTickCount();
        estado(L"No aparece el celular: reiniciando adb...");
        b->cambio_estado = 1;
        dispositivo_reiniciar_adb(adb);
        dispositivo_buscar(adb, g->conexion, d);
    }
    if (d->estado == DISP_NINGUNO && g->conexion != CONEXION_USB && sin > 10000 && !b->qr_ofrecido) {
        b->qr_ofrecido = 1;
        estado(L"No hay celular por cable ni por WiFi. Escaneá el código QR con el celular.");
        b->cambio_estado = 1;
        abrir_qr(0);
    }
}

static int esperar_reconexion(const ConfigGeneral *g, const wchar_t *adb, Dispositivo *d)
{
    Dispositivo nuevo;
    DWORD ultimo_wifi = 0;
    InterlockedExchange(&s_cancelar, 0);
    cambiar_modo(MODO_RECONECTANDO, 0);
    estado(L"Se desconectó el celular. Volvé a conectarlo: la imagen vuelve sola.");
    while (!s_salir && !s_cancelar) {
        dispositivo_buscar(adb, g->conexion, &nuevo);
        if (nuevo.estado != DISP_LISTO && g->conexion != CONEXION_USB && GetTickCount() - ultimo_wifi > 5000) {
            ultimo_wifi = GetTickCount();
            if (dispositivo_conectar_wifi(adb, 1))
                dispositivo_buscar(adb, g->conexion, &nuevo);
        }
        if (nuevo.estado == DISP_LISTO) {
            Sleep(1000); /* dar tiempo a que el celular termine de conectarse */
            *d = nuevo;
            return 1;
        }
        Sleep(1000);
    }
    return 0;
}

static void jugar(Dispositivo *d, const wchar_t *adb)
{
    ConfigGeneral g;
    Perfil p;
    static wchar_t cmd[RTX_CMD_LEN], avisos[1024], error[2048], texto[3072];
    DWORD codigo;
    int caidas_rapidas = 0, formato_cambiado = 0;

    config_leer_general(&g);
    s_cerrar_al_terminar = g.cerrar_al_terminar;
    if (!config_leer_perfil(g.perfil, &p))
        log_escribir(L"El perfil '%ls' no existe en el .ini: se usan valores por defecto", g.perfil);

    if (p.formato_monitor) {
        /* scrcpy abre la pantalla completa en el monitor principal */
        formato_cambiado = formato_aplicar(adb, d, GetSystemMetrics(SM_CXSCREEN),
                                           GetSystemMetrics(SM_CYSCREEN), texto, 3072);
        estado(texto);
        if (formato_cambiado)
            Sleep(800); /* que el celular termine de reacomodar la pantalla antes de capturarla */
    }

    for (;;) {
        DWORD inicio;
        sesion_armar_comando(&p, d, cmd, RTX_CMD_LEN, avisos, 1024);
        if (avisos[0])
            log_escribir(L"Avisos: %ls", avisos);
        estado(avisos[0] ? avisos : L"Abriendo la imagen del celular...");
        cambiar_modo(MODO_JUGANDO, 0);

        inicio = GetTickCount();
        /* se vigila el audio mientras quede un codec alternativo para probar */
        codigo = sesion_ejecutar(cmd, p.audio && _wcsicmp(p.codec_audio, L"raw"), error, 2048);

        if (codigo == SESION_FALLO_AUDIO) {
            const wchar_t *siguiente = !_wcsicmp(p.codec_audio, L"aac") ? L"raw" : L"aac";
            log_escribir(L"El códec de audio '%ls' no funciona en este celular; se prueba '%ls'. "
                         L"Para que sea permanente poné codec_audio=%ls en el perfil.",
                         p.codec_audio, siguiente, siguiente);
            wcscpy(p.codec_audio, siguiente);
            continue;
        }
        /* cortes seguidos apenas arranca = algo anda mal: no reintentar para siempre */
        caidas_rapidas = (GetTickCount() - inicio < 5000) ? caidas_rapidas + 1 : 0;

        if (codigo == 2 && g.reconectar && !s_salir && caidas_rapidas < 3) {
            if (esperar_reconexion(&g, adb, d))
                continue;
            /* sin celular no se puede restaurar: queda pendiente en restaurar.ini */
            estado(L"Reconexión cancelada.");
            cambiar_modo(MODO_LISTO, 0);
            return;
        }
        break;
    }

    /* si se desconecto, no responde: se reintenta sola al volver a detectarlo */
    if (formato_cambiado)
        formato_restaurar_pendiente(adb, d);

    if (codigo == 0) {
        estado(L"Sesión terminada. Tocá Jugar para volver a empezar.");
    } else if (codigo == 2) {
        estado(caidas_rapidas >= 3 ? L"La conexión se corta apenas empieza. Probá otro cable o puerto USB."
                                   : L"Se desconectó el celular.");
    } else if (!s_salir) {
        swprintf(texto, 3072, L"scrcpy no pudo iniciar (código %lu).\n\n%ls\n"
                              L"Más detalles en \"Ver registro\".",
                 codigo, error[0] ? error : L"Sin mensaje de error.");
        estado(L"No se pudo abrir la imagen. Mirá el detalle del error.");
        enviar_texto(WM_RTX_ERROR, 0, texto);
    }
    cambiar_modo(MODO_LISTO, codigo);
}

static DWORD WINAPI hilo_trabajo(LPVOID arg)
{
    ConfigGeneral g;
    Dispositivo d;
    wchar_t adb[MAX_PATH], desc[256], previo[256] = L"", error[512];
    int auto_pendiente, quiere_jugar, listo_antes = 0;
    Busqueda busq = { 0 };
    (void)arg;

    config_leer_general(&g);
    auto_pendiente = g.inicio_automatico;

    /* 1) motor: scrcpy + adb */
    while (!motor_listo(g.version_scrcpy)) {
        cambiar_modo(MODO_OCUPADO, 0);
        estado(L"Falta scrcpy/adb: descargándolo (solo la primera vez)...");
        if (motor_descargar(g.version_scrcpy, progreso_descarga, &s_salir, error, 512))
            break;
        PostMessageW(s_ventana, WM_RTX_PROGRESO, (WPARAM)-1, 0);
        if (s_salir)
            return 0;
        estado(L"No se pudo descargar el motor. Tocá Reintentar.");
        enviar_texto(WM_RTX_ERROR, 0, error);
        cambiar_modo(MODO_FALLO_MOTOR, 0);
        WaitForSingleObject(s_evento_jugar, INFINITE);
        if (s_salir)
            return 0;
        config_leer_general(&g);
    }
    PostMessageW(s_ventana, WM_RTX_PROGRESO, (WPARAM)-1, 0);

    /* scrcpy usa la variable ADB para saber que adb ejecutar: asi todo usa el mismo */
    motor_ruta_adb(&g, adb, MAX_PATH);
    SetEnvironmentVariableW(L"ADB", adb);
    log_escribir(L"adb: %ls", adb);
    estado(L"Iniciando adb...");
    dispositivo_iniciar_adb(adb);
    cambiar_modo(MODO_LISTO, 0);
    estado(L"Listo. Elegí un perfil y tocá Jugar.");

    /* 2) vigilar el celular (por cable y por WiFi) y esperar el boton Jugar */
    busq.sin_celular_desde = GetTickCount();
    while (!s_salir) {
        DWORD r;
        config_leer_general(&g);
        dispositivo_buscar(adb, g.conexion, &d);
        if (d.estado != DISP_LISTO && listo_antes)
            busq.sin_celular_desde = GetTickCount(); /* se acaba de perder: los tiempos cuentan desde ahora */
        if (g_qr_activo)
            emparejar_qr(&g, adb, &d);
        else if (d.estado != DISP_LISTO)
            buscar_de_otra_forma(&g, adb, &d, &busq);

        if (d.estado == DISP_LISTO && !listo_antes) {
            /* aparecio (por cable o WiFi): el QR ya no hace falta */
            if (g_qr_activo)
                cerrar_qr();
            busq.qr_ofrecido = 0;
            busq.ultimo_reinicio = 0;
            if (busq.cambio_estado)
                estado(L"Listo. Elegí un perfil y tocá Jugar.");
            busq.cambio_estado = 0;
        }
        listo_antes = d.estado == DISP_LISTO;

        dispositivo_describir(&d, desc, 256);
        if (wcscmp(desc, previo)) {
            wcscpy(previo, desc);
            log_escribir(L"Celular: %ls", desc);
            enviar_texto(WM_RTX_CELULAR, d.estado == DISP_LISTO, desc);
            /* quedo una resolucion cambiada de una sesion que termino mal? */
            if (d.estado == DISP_LISTO)
                formato_restaurar_pendiente(adb, &d);
        }

        if (auto_pendiente && d.estado == DISP_LISTO) {
            auto_pendiente = 0;
            quiere_jugar = 1;
        } else {
            HANDLE eventos[2] = { s_evento_jugar, s_evento_qr };
            r = WaitForMultipleObjects(2, eventos, FALSE, g_qr_activo ? 1000 : 1500);
            if (r == WAIT_OBJECT_0 + 1 && !s_salir) {
                abrir_qr(0);
                continue;
            }
            quiere_jugar = r == WAIT_OBJECT_0;
        }
        if (s_salir)
            break;
        if (!quiere_jugar)
            continue;

        if (d.estado != DISP_LISTO) {
            /* tocaron Jugar sin celular: se prueba todo ya, sin esperar los tiempos de la busqueda */
            estado(L"Buscando el celular...");
            busq.cambio_estado = 1;
            if (g.conexion != CONEXION_WIFI && !g_qr_activo && d.estado == DISP_NINGUNO)
                dispositivo_reiniciar_adb(adb);
            if (g.conexion != CONEXION_USB)
                dispositivo_conectar_wifi(adb, 1);
            dispositivo_buscar(adb, g.conexion, &d);
            if (d.estado != DISP_LISTO) {
                dispositivo_describir(&d, desc, 256);
                estado(desc);
                if (d.estado == DISP_NINGUNO && g.conexion != CONEXION_USB)
                    abrir_qr(0);
                continue;
            }
        }
        listo_antes = 1;
        busq.cambio_estado = 0;
        jugar(&d, adb);
        previo[0] = 0; /* refrescar la descripcion al volver */
    }
    return 0;
}

/* ---------- ventana ---------- */

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

static HWND crear(const wchar_t *clase, const wchar_t *texto, DWORD estilo, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, clase, texto, WS_CHILD | WS_VISIBLE | estilo, S(x), S(y), S(w), S(h),
                             s_ventana, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)s_fuente, TRUE);
    return c;
}

static void cargar_perfiles(void)
{
    static wchar_t nombres[RTX_MAX_PERFILES][RTX_NOMBRE_LEN];
    ConfigGeneral g;
    int n, i, elegido = 0;

    config_leer_general(&g);
    n = config_listar_perfiles(nombres, RTX_MAX_PERFILES);
    SendMessageW(s_combo, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < n; i++) {
        SendMessageW(s_combo, CB_ADDSTRING, 0, (LPARAM)nombres[i]);
        if (!_wcsicmp(nombres[i], g.perfil))
            elegido = i;
    }
    if (n == 0)
        SendMessageW(s_combo, CB_ADDSTRING, 0, (LPARAM)g.perfil);
    SendMessageW(s_combo, CB_SETCURSEL, elegido, 0);
}

static void guardar_perfil_elegido(void)
{
    wchar_t nombre[RTX_NOMBRE_LEN];
    int i = (int)SendMessageW(s_combo, CB_GETCURSEL, 0, 0);
    if (i >= 0 && SendMessageW(s_combo, CB_GETLBTEXTLEN, i, 0) < RTX_NOMBRE_LEN) {
        SendMessageW(s_combo, CB_GETLBTEXT, i, (LPARAM)nombre);
        config_guardar_perfil_elegido(nombre);
    }
}

static void aplicar_modo(Modo m, DWORD codigo)
{
    s_modo = m;
    EnableWindow(s_btn_qr, m == MODO_LISTO);
    switch (m) {
    case MODO_JUGANDO:
        ShowWindow(s_ventana, SW_HIDE);
        return;
    case MODO_RECONECTANDO:
        SetWindowTextW(s_btn_jugar, L"Cancelar");
        EnableWindow(s_btn_jugar, TRUE);
        break;
    case MODO_OCUPADO:
        SetWindowTextW(s_btn_jugar, L"▶  Jugar");
        EnableWindow(s_btn_jugar, FALSE);
        break;
    case MODO_FALLO_MOTOR:
        SetWindowTextW(s_btn_jugar, L"Reintentar");
        EnableWindow(s_btn_jugar, TRUE);
        break;
    default:
        if (codigo == 0 && s_cerrar_al_terminar && IsWindowVisible(s_ventana) == FALSE) {
            PostMessageW(s_ventana, WM_CLOSE, 0, 0);
            return;
        }
        SetWindowTextW(s_btn_jugar, L"▶  Jugar");
        EnableWindow(s_btn_jugar, TRUE);
        break;
    }
    if (!IsWindowVisible(s_ventana)) {
        s_mostrada_en = GetTickCount();
        if (m == MODO_RECONECTANDO) {
            /* aviso sin robar el foco: puede aparecer mientras se usa otra cosa */
            ShowWindow(s_ventana, SW_SHOWNOACTIVATE);
        } else {
            ShowWindow(s_ventana, SW_SHOWNORMAL);
            SetForegroundWindow(s_ventana);
        }
    }
}

static void abrir_en_bloc(const wchar_t *ruta)
{
    ShellExecuteW(s_ventana, L"open", L"notepad.exe", ruta, NULL, SW_SHOWNORMAL);
}

static LRESULT CALLBACK proc_ventana(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HWND t;
        s_ventana = h;
        t = crear(L"STATIC", RTX_NOMBRE, 0, 20, 14, 300, 34, 0);
        SendMessageW(t, WM_SETFONT, (WPARAM)s_fuente_titulo, TRUE);
        crear(L"STATIC", L"Pantalla y sonido del celular en tu PC", 0, 22, 48, 400, 18, 0);

        crear(L"STATIC", L"Celular:", 0, 22, 82, 70, 18, 0);
        s_lbl_celular = crear(L"STATIC", L"Buscando...", SS_ENDELLIPSIS, 92, 82, 360, 18, 0);

        crear(L"STATIC", L"Perfil:", 0, 22, 116, 70, 18, 0);
        s_combo = crear(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 92, 112, 250, 300, ID_PERFIL);
        crear(L"BUTTON", L"Editar perfiles", WS_TABSTOP, 350, 111, 102, 26, ID_EDITAR);

        s_progreso = crear(PROGRESS_CLASSW, L"", 0, 22, 152, 430, 14, 0);
        SendMessageW(s_progreso, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        ShowWindow(s_progreso, SW_HIDE);
        s_estado = crear(L"STATIC", L"Preparando...", 0, 22, 172, 430, 52, 0);

        s_btn_jugar = crear(L"BUTTON", L"▶  Jugar", BS_DEFPUSHBUTTON | WS_TABSTOP, 22, 232, 200, 40, ID_JUGAR);
        EnableWindow(s_btn_jugar, FALSE);
        crear(L"BUTTON", L"Ver registro", WS_TABSTOP, 232, 232, 106, 40, ID_REGISTRO);
        crear(L"BUTTON", L"Salir", WS_TABSTOP, 346, 232, 106, 40, ID_SALIR);
        s_btn_qr = crear(L"BUTTON", L"Conectar por WiFi (QR)", WS_TABSTOP, 22, 280, 200, 30, ID_QR);
        EnableWindow(s_btn_qr, FALSE);
        crear(L"STATIC", L"Mientras jugás:  F11 = pantalla completa  ·  Alt+F4 = terminar", 0, 22, 322, 430, 18, 0);

        cargar_perfiles();
        return 0;
    }

    case WM_ACTIVATE:
        /* al volver (por ejemplo, despues de editar el .ini) se recargan los perfiles */
        if (LOWORD(wp) != WA_INACTIVE) {
            /* activada con un clic: ese clic puede ser justo sobre Jugar, no se frena */
            if (LOWORD(wp) == WA_ACTIVE)
                s_mostrada_en = GetTickCount();
            if (s_combo && s_modo != MODO_JUGANDO)
                cargar_perfiles();
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_PERFIL:
            if (HIWORD(wp) == CBN_SELCHANGE)
                guardar_perfil_elegido();
            return 0;
        case ID_EDITAR:
            abrir_en_bloc(g_rutas.ini);
            return 0;
        case ID_QR:
            SetEvent(s_evento_qr);
            return 0;
        case ID_REGISTRO:
            abrir_en_bloc(g_rutas.log);
            return 0;
        case ID_SALIR:
            PostMessageW(h, WM_CLOSE, 0, 0);
            return 0;
        case ID_JUGAR:
            if (GetTickCount() - s_mostrada_en < ESPERA_ANTI_REBOTE_MS) {
                log_escribir(L"Toque en Jugar ignorado (la ventana acaba de aparecer)");
            } else if (s_modo == MODO_RECONECTANDO) {
                InterlockedExchange(&s_cancelar, 1);
            } else {
                log_escribir(L"Botón Jugar (%ls)", (GetKeyState(VK_LBUTTON) & 0x8000) ? L"mouse" : L"teclado");
                guardar_perfil_elegido();
                SetEvent(s_evento_jugar);
            }
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == s_lbl_celular) {
            HDC dc = (HDC)wp;
            SetTextColor(dc, s_celular_listo ? RGB(0, 140, 60) : RGB(200, 100, 0));
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;

    case WM_RTX_ESTADO:
        SetWindowTextW(s_estado, (wchar_t *)lp);
        free((void *)lp);
        return 0;

    case WM_RTX_CELULAR:
        s_celular_listo = (int)wp;
        SetWindowTextW(s_lbl_celular, (wchar_t *)lp);
        InvalidateRect(s_lbl_celular, NULL, TRUE);
        free((void *)lp);
        return 0;

    case WM_RTX_PROGRESO:
        if ((int)wp < 0) {
            ShowWindow(s_progreso, SW_HIDE);
        } else {
            ShowWindow(s_progreso, SW_SHOW);
            SendMessageW(s_progreso, PBM_SETPOS, wp, 0);
        }
        return 0;

    case WM_RTX_MODO:
        aplicar_modo((Modo)wp, (DWORD)lp);
        return 0;

    case WM_RTX_ERROR:
        MessageBoxW(h, (wchar_t *)lp, RTX_NOMBRE, MB_OK | MB_ICONWARNING);
        free((void *)lp);
        return 0;

    case WM_RTX_QR:
        if (wp) {
            qr_mostrar(h, s_fuente, (const char *)lp);
            free((void *)lp);
        } else {
            qr_cerrar();
        }
        return 0;

    case WM_RTX_QR_ESTADO:
        qr_estado((wchar_t *)lp);
        free((void *)lp);
        return 0;

    case WM_CLOSE:
        InterlockedExchange(&s_salir, 1);
        InterlockedExchange(&s_cancelar, 1);
        SetEvent(s_evento_jugar);
        if (s_hilo && WaitForSingleObject(s_hilo, 4000) == WAIT_TIMEOUT)
            log_escribir(L"El hilo de trabajo no terminó a tiempo");
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void iniciar_rutas(void)
{
    wchar_t *barra;
    GetModuleFileNameW(NULL, g_rutas.base, MAX_PATH);
    barra = wcsrchr(g_rutas.base, L'\\');
    if (barra)
        *barra = 0;
    ruta_unir(g_rutas.ini, MAX_PATH, g_rutas.base, L"rtxjuegos.ini");
    ruta_unir(g_rutas.motor, MAX_PATH, g_rutas.base, L"motor");
    ruta_unir(g_rutas.log, MAX_PATH, g_rutas.base, L"rtxjuegos.log");
}

static void crear_fuentes(void)
{
    NONCLIENTMETRICSW ncm = { sizeof ncm };
    LOGFONTW lf;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    s_fuente = CreateFontIndirectW(&ncm.lfMessageFont);
    lf = ncm.lfMessageFont;
    lf.lfHeight = -S(24);
    lf.lfWeight = FW_BOLD;
    s_fuente_titulo = CreateFontIndirectW(&lf);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR linea, int mostrar)
{
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    WNDCLASSEXW wc = { sizeof wc };
    RECT r;
    MSG m;
    HANDLE unico;
    HWND h;
    (void)prev;
    (void)linea;

    /* una sola copia abierta: si ya hay otra, se trae al frente */
    unico = CreateMutexW(NULL, TRUE, L"RTXJuegos.InstanciaUnica");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        h = FindWindowW(L"RTXJuegosVentana", NULL);
        if (h) {
            ShowWindow(h, SW_SHOWNORMAL);
            SetForegroundWindow(h);
        }
        return 0;
    }

    iniciar_rutas();
    log_iniciar();
    log_escribir(L"%ls %ls - carpeta %ls", RTX_NOMBRE, RTX_VERSION, g_rutas.base);
    config_asegurar_ini();

    InitCommonControlsEx(&icc);
    crear_fuentes();

    wc.lpfnWndProc = proc_ventana;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"RTXJuegosVentana";
    RegisterClassExW(&wc);

    r.left = 0;
    r.top = 0;
    r.right = S(474);
    r.bottom = S(350);
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    h = CreateWindowExW(0, wc.lpszClassName, RTX_NOMBRE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!h)
        return 1;

    s_evento_jugar = CreateEventW(NULL, FALSE, FALSE, NULL);
    s_evento_qr = CreateEventW(NULL, FALSE, FALSE, NULL);
    s_hilo = CreateThread(NULL, 0, hilo_trabajo, NULL, 0, NULL);

    ShowWindow(h, mostrar);
    UpdateWindow(h);

    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(h, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (unico)
        CloseHandle(unico);
    return (int)m.wParam;
}
