/*
 * RTXJuegos - espejo de pantalla y sonido del celular en la PC (via scrcpy + adb).
 * Declaraciones compartidas entre los modulos.
 */
#ifndef RTXJUEGOS_H
#define RTXJUEGOS_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#define RTX_NOMBRE        L"RTXJuegos"
#define RTX_VERSION       L"1.1.0"
#define RTX_SCRCPY_DEF    L"5.0.1"
/* SHA256 oficial de scrcpy-win64-v5.0.1.zip (respaldo si no se puede leer SHA256SUMS.txt) */
#define RTX_SHA256_DEF    "b12a2c4ee8be317422451fc7dcf8ee20a71b5ea7ef9ad73ddd825a25316227a5"

#define RTX_MAX_PERFILES  32
#define RTX_NOMBRE_LEN    64
#define RTX_CMD_LEN       4096

/* Mensajes del hilo de trabajo hacia la ventana (lParam = wchar_t* creado con _wcsdup, lo libera la ventana) */
#define WM_RTX_ESTADO     (WM_APP + 1)   /* texto de estado */
#define WM_RTX_CELULAR    (WM_APP + 2)   /* wParam = 1 si hay celular listo, lParam = descripcion */
#define WM_RTX_PROGRESO   (WM_APP + 3)   /* wParam = porcentaje 0..100, o (WPARAM)-1 para ocultar */
#define WM_RTX_MODO       (WM_APP + 4)   /* wParam = Modo, lParam = codigo de salida de scrcpy */
#define WM_RTX_ERROR      (WM_APP + 5)   /* lParam = mensaje para mostrar en cuadro de dialogo */
#define WM_RTX_QR         (WM_APP + 6)   /* wParam = 1 abrir el QR (lParam = texto char*, o NULL para traerlo al frente), 0 cerrarlo */
#define WM_RTX_QR_ESTADO  (WM_APP + 7)   /* lParam = texto para la ventana del QR */

typedef enum {
    MODO_LISTO,         /* esperando que toquen Jugar */
    MODO_JUGANDO,       /* scrcpy abierto: la ventana se oculta */
    MODO_RECONECTANDO,  /* se desconecto el cable: el boton pasa a "Cancelar" */
    MODO_OCUPADO,       /* descargando el motor */
    MODO_FALLO_MOTOR    /* no se pudo descargar: el boton pasa a "Reintentar" */
} Modo;

/* ---------- Rutas ---------- */
typedef struct {
    wchar_t base[MAX_PATH];    /* carpeta donde esta rtxjuegos.exe */
    wchar_t ini[MAX_PATH];     /* base\rtxjuegos.ini */
    wchar_t motor[MAX_PATH];   /* base\motor (scrcpy + adb) */
    wchar_t log[MAX_PATH];     /* base\rtxjuegos.log */
} Rutas;

extern Rutas g_rutas;

/* ---------- Configuracion (config.c) ---------- */
typedef enum { CONEXION_AUTO, CONEXION_USB, CONEXION_WIFI } Conexion;

typedef struct {
    wchar_t  perfil[RTX_NOMBRE_LEN];
    Conexion conexion;
    int      reconectar;
    int      inicio_automatico;
    int      cerrar_al_terminar;
    wchar_t  version_scrcpy[32];
    wchar_t  adb[MAX_PATH];        /* "motor", "sistema" o una ruta a adb.exe */
} ConfigGeneral;

typedef struct {
    wchar_t nombre[RTX_NOMBRE_LEN];
    int     resolucion;            /* 0 = nativa */
    int     fps;                   /* 0 = sin limite */
    wchar_t bitrate[16];
    wchar_t codec_video[16];
    wchar_t codificador_video[64];
    wchar_t decodificador[16];
    int     buffer_video;
    int     audio;
    wchar_t codec_audio[16];
    wchar_t bitrate_audio[16];
    int     buffer_audio;
    int     audio_en_celular;
    int     pantalla_completa;
    int     apagar_pantalla_celular;
    int     mantener_despierto;
    int     siempre_encima;
    int     sin_bordes;
    wchar_t ajuste[16];
    wchar_t orientacion[16];
    wchar_t mando_pc[16];
    int     apagar_celular_al_salir;
    int     formato_monitor;       /* 1 = pasar el celular a la proporcion del monitor al jugar */
    wchar_t extra[1024];
} Perfil;

void config_asegurar_ini(void);
void config_leer_general(ConfigGeneral *g);
void config_guardar_perfil_elegido(const wchar_t *nombre);
int  config_listar_perfiles(wchar_t nombres[][RTX_NOMBRE_LEN], int max);
int  config_leer_perfil(const wchar_t *nombre, Perfil *p);

/* ---------- Utilidades (util.c) ---------- */
void log_iniciar(void);
void log_escribir(const wchar_t *fmt, ...);
void log_crudo(const char *datos, DWORD n);

int  archivo_existe(const wchar_t *ruta);
int  carpeta_existe(const wchar_t *ruta);
int  borrar_carpeta(const wchar_t *ruta);
void ruta_unir(wchar_t *dest, size_t cap, const wchar_t *a, const wchar_t *b);

/* Ejecuta un programa oculto y captura stdout+stderr (texto ANSI/UTF-8). Devuelve 1 si se pudo ejecutar. */
int  ejecutar_capturando(const wchar_t *cmd, char *salida, size_t cap, DWORD espera_ms, DWORD *codigo);

void cmd_agregar(wchar_t *cmd, size_t cap, const wchar_t *fmt, ...);

/* ---------- Motor scrcpy/adb (motor.c) ---------- */
typedef void (*ProgresoFn)(int porcentaje, const wchar_t *texto);

int  motor_listo(const wchar_t *version);
int  motor_descargar(const wchar_t *version, ProgresoFn progreso, volatile LONG *cancelar,
                     wchar_t *error, size_t cap_error);
void motor_ruta_scrcpy(wchar_t *dest, size_t cap);
void motor_ruta_adb(const ConfigGeneral *g, wchar_t *dest, size_t cap);

/* ---------- Celular (dispositivo.c) ---------- */
typedef enum {
    DISP_NINGUNO, DISP_LISTO, DISP_SIN_PERMISO, DISP_CONECTANDO, DISP_OFFLINE, DISP_SOLO_OTRA_CONEXION
} EstadoDisp;

typedef struct {
    EstadoDisp estado;
    wchar_t    serial[128];     /* para adb -s: cambia segun USB/WiFi */
    wchar_t    id[128];         /* ro.serialno: el mismo celular siempre tiene el mismo */
    int        es_usb;
    wchar_t    modelo[128];
    int        sdk;             /* nivel de API de Android (30 = Android 11) */
    wchar_t    android[16];
} Dispositivo;

void dispositivo_iniciar_adb(const wchar_t *adb);
void dispositivo_buscar(const wchar_t *adb, Conexion conexion, Dispositivo *d);
void dispositivo_describir(const Dispositivo *d, wchar_t *dest, size_t cap);
/* adb kill-server + start-server: lo que hacia falta hacer a mano cuando no aparecia el celular */
void dispositivo_reiniciar_adb(const wchar_t *adb);
void dispositivo_reconectar_offline(const wchar_t *adb);
/* "adb connect" a los celulares ya emparejados que se anuncian en la red (mDNS). Devuelve 1 si
   se conecto alguno. Sin 'forzar' no reintenta los que fallaron hace poco. */
int  dispositivo_conectar_wifi(const wchar_t *adb, int forzar);
/* Si el celular ya escaneo el QR (servicio mDNS 'nombre'), hace "adb pair" y "adb connect".
   Devuelve 0 si todavia no aparece, 1 si quedo emparejado, -1 si fallo el emparejamiento. */
typedef void (*AvisoFn)(const wchar_t *texto);
int  dispositivo_emparejar(const wchar_t *adb, const char *nombre, const char *clave, AvisoFn aviso);

/* ---------- Ventana del QR para emparejar por WiFi (qr.c) ---------- */
extern volatile LONG g_qr_activo;   /* 1 mientras la ventana del QR esta abierta */
void qr_nuevos_datos(char *nombre, size_t cap_nombre, char *clave, size_t cap_clave);
/* Abre la ventana con el QR de 'texto' (NULL = traer al frente la que ya esta abierta) */
void qr_mostrar(HWND dueno, HFONT fuente, const char *texto);
void qr_estado(const wchar_t *texto);
void qr_cerrar(void);

/* ---------- Formato del monitor (formato.c) ---------- */
/* Pasa el celular a la proporcion del monitor. Devuelve 1 si cambio algo (hay que restaurar). */
int  formato_aplicar(const wchar_t *adb, const Dispositivo *d, int mon_w, int mon_h,
                     wchar_t *info, size_t cap);
/* Si quedo una resolucion cambiada de este celular (restaurar.ini), la devuelve a la original. */
void formato_restaurar_pendiente(const wchar_t *adb, const Dispositivo *d);

/* ---------- Sesion de juego (sesion.c) ---------- */
/* Arma la linea de comandos de scrcpy para el perfil y el celular. Agrega avisos a 'avisos'. */
void sesion_armar_comando(const Perfil *p, const Dispositivo *d, wchar_t *cmd, size_t cap,
                          wchar_t *avisos, size_t cap_avisos);
/* Codigo propio: el audio fallo y se corto scrcpy para reintentar con otro codec */
#define SESION_FALLO_AUDIO 77
/* Ejecuta scrcpy y espera a que termine. Devuelve su codigo de salida (0 normal, 1 fallo,
   2 desconectado) o SESION_FALLO_AUDIO si vigilar_audio y el audio no pudo iniciar. */
DWORD sesion_ejecutar(const wchar_t *cmd, int vigilar_audio, wchar_t *ultimo_error, size_t cap);

#endif
