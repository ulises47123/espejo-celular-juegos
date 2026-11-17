/*
 * Sesion de juego: arma los argumentos de scrcpy a partir del perfil y lo ejecuta,
 * guardando toda su salida en el registro.
 */
#include "rtxjuegos.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static void aviso(wchar_t *avisos, size_t cap, const wchar_t *texto)
{
    if (wcslen(avisos) + wcslen(texto) + 2 < cap) {
        if (avisos[0])
            wcscat(avisos, L"\n");
        wcscat(avisos, texto);
    }
}

void sesion_armar_comando(const Perfil *p, const Dispositivo *d, wchar_t *cmd, size_t cap,
                          wchar_t *avisos, size_t cap_avisos)
{
    wchar_t scrcpy[MAX_PATH];
    int audio = p->audio;

    avisos[0] = 0;
    cmd[0] = 0;
    motor_ruta_scrcpy(scrcpy, MAX_PATH);
    cmd_agregar(cmd, cap, L"\"%ls\"", scrcpy);
    cmd_agregar(cmd, cap, L"--serial=\"%ls\"", d->serial);
    cmd_agregar(cmd, cap, L"--window-title=\"%ls - %ls\"", RTX_NOMBRE, p->nombre);

    /* ---- video ---- */
    if (p->resolucion > 0)
        cmd_agregar(cmd, cap, L"--max-size=%d", p->resolucion);
    if (p->fps > 0)
        cmd_agregar(cmd, cap, L"--max-fps=%d", p->fps);
    if (p->bitrate[0])
        cmd_agregar(cmd, cap, L"--video-bit-rate=%ls", p->bitrate);
    if (p->codec_video[0])
        cmd_agregar(cmd, cap, L"--video-codec=%ls", p->codec_video);
    if (p->codificador_video[0])
        cmd_agregar(cmd, cap, L"--video-encoder=%ls", p->codificador_video);
    if (p->decodificador[0])
        cmd_agregar(cmd, cap, L"--hwdec=%ls", p->decodificador);
    if (p->buffer_video > 0)
        cmd_agregar(cmd, cap, L"--video-buffer=%d", p->buffer_video);
    if (p->orientacion[0])
        cmd_agregar(cmd, cap, L"--capture-orientation=%ls", p->orientacion);

    /* ---- audio ---- */
    if (audio && d->sdk > 0 && d->sdk < 30) {
        aviso(avisos, cap_avisos, L"Este celular tiene Android 10 o anterior: no se puede pasar el sonido, solo la imagen.");
        audio = 0;
    }
    if (audio) {
        if (d->sdk == 30)
            aviso(avisos, cap_avisos, L"Android 11: para que haya sonido el celular tiene que estar desbloqueado al empezar.");
        if (p->codec_audio[0])
            cmd_agregar(cmd, cap, L"--audio-codec=%ls", p->codec_audio);
        if (p->bitrate_audio[0] && _wcsicmp(p->codec_audio, L"raw"))
            cmd_agregar(cmd, cap, L"--audio-bit-rate=%ls", p->bitrate_audio);
        if (p->buffer_audio > 0)
            cmd_agregar(cmd, cap, L"--audio-buffer=%d", p->buffer_audio);
        if (p->audio_en_celular) {
            if (d->sdk >= 33)
                cmd_agregar(cmd, cap, L"--audio-source=playback --audio-dup");
            else
                aviso(avisos, cap_avisos, L"'audio_en_celular' necesita Android 13 o superior; el sonido sale solo por la PC.");
        }
    } else {
        cmd_agregar(cmd, cap, L"--no-audio");
    }

    /* ---- ventana ---- */
    if (p->pantalla_completa)
        cmd_agregar(cmd, cap, L"--fullscreen");
    if (p->siempre_encima)
        cmd_agregar(cmd, cap, L"--always-on-top");
    if (p->sin_bordes)
        cmd_agregar(cmd, cap, L"--window-borderless");
    if (p->ajuste[0] && _wcsicmp(p->ajuste, L"letterbox"))
        cmd_agregar(cmd, cap, L"--render-fit=%ls", p->ajuste);

    /* ---- celular ---- */
    if (p->apagar_pantalla_celular)
        cmd_agregar(cmd, cap, L"--turn-screen-off");
    if (p->mantener_despierto)
        cmd_agregar(cmd, cap, L"--stay-awake");
    if (p->apagar_celular_al_salir)
        cmd_agregar(cmd, cap, L"--power-off-on-close");
    if (p->mando_pc[0] && _wcsicmp(p->mando_pc, L"no")) {
        if (!_wcsicmp(p->mando_pc, L"aoa") && !d->es_usb)
            aviso(avisos, cap_avisos, L"mando_pc=aoa solo funciona por USB; se usa uhid.");
        cmd_agregar(cmd, cap, L"--gamepad=%ls",
                    (!_wcsicmp(p->mando_pc, L"aoa") && d->es_usb) ? L"aoa" : L"uhid");
    }

    if (p->extra[0])
        cmd_agregar(cmd, cap, L"%ls", p->extra);
}

/* Guarda las ultimas lineas que empiezan con "ERROR:" para mostrarselas al usuario */
static void recordar_errores(const char *bloque, char *errores, size_t cap)
{
    const char *p = bloque;
    while ((p = strstr(p, "ERROR:")) != NULL) {
        const char *fin = strpbrk(p, "\r\n");
        size_t len = fin ? (size_t)(fin - p) : strlen(p);
        size_t usado = strlen(errores);
        if (usado + len + 2 >= cap) { /* sin lugar: quedarse con lo mas reciente */
            errores[0] = 0;
            usado = 0;
        }
        if (len + 2 < cap) {
            memcpy(errores + usado, p, len);
            errores[usado + len] = '\n';
            errores[usado + len + 1] = 0;
        }
        p += len;
    }
}

typedef struct {
    HANDLE tuberia;
    HANDLE proceso;
    int    vigilar_audio;
    int    audio_fallo;
    char  *errores;
} Lector;

/* Mensajes de scrcpy cuando el codec de audio no anda en el celular (ver issues #5452 y #5608) */
static int es_falla_audio(const char *texto)
{
    return strstr(texto, "Demuxer 'audio': stream explicitly disabled by the device") ||
           strstr(texto, "Failed to initialize audio") ||
           (strstr(texto, "audio-encoder") && strstr(texto, "Exception"));
}

static DWORD WINAPI hilo_lector(LPVOID arg)
{
    Lector *l = (Lector *)arg;
    /* se conservan los ultimos bytes del bloque anterior por si un mensaje queda partido */
    char buf[256 + 4097];
    size_t arrastre = 0;
    DWORD leido;
    while (ReadFile(l->tuberia, buf + arrastre, 4096, &leido, NULL) && leido > 0) {
        size_t total = arrastre + leido;
        buf[total] = 0;
        log_crudo(buf + arrastre, leido);
        recordar_errores(buf + arrastre, l->errores, 2048);
        if (l->vigilar_audio && !l->audio_fallo && es_falla_audio(buf)) {
            l->audio_fallo = 1;
            log_escribir(L"Falló el audio: se reinicia la sesión con otro códec");
            TerminateProcess(l->proceso, SESION_FALLO_AUDIO);
        }
        arrastre = total < 256 ? total : 256;
        memmove(buf, buf + total - arrastre, arrastre);
    }
    return 0;
}

DWORD sesion_ejecutar(const wchar_t *cmd, int vigilar_audio, wchar_t *ultimo_error, size_t cap)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE lectura, escritura, hilo;
    Lector lector;
    wchar_t *linea;
    static char errores[2048];
    DWORD codigo = 1;

    ultimo_error[0] = 0;
    errores[0] = 0;
    log_escribir(L"Ejecutando: %ls", cmd);

    if (!CreatePipe(&lectura, &escritura, &sa, 0)) {
        swprintf(ultimo_error, cap, L"No se pudo crear la tubería de salida.");
        return 1;
    }
    SetHandleInformation(lectura, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = escritura;
    si.hStdError = escritura;

    linea = _wcsdup(cmd);
    if (!CreateProcessW(NULL, linea, NULL, NULL, TRUE, CREATE_NO_WINDOW | ABOVE_NORMAL_PRIORITY_CLASS,
                        NULL, g_rutas.motor, &si, &pi)) {
        swprintf(ultimo_error, cap, L"No se pudo abrir scrcpy (error %lu).", GetLastError());
        free(linea);
        CloseHandle(lectura);
        CloseHandle(escritura);
        return 1;
    }
    free(linea);
    CloseHandle(escritura);

    /* Si scrcpy llegara a arrancar el servidor adb, este heredaria la tuberia y nunca daria EOF:
       por eso se lee en otro hilo y se corta cuando scrcpy termina. */
    lector.tuberia = lectura;
    lector.proceso = pi.hProcess;
    lector.vigilar_audio = vigilar_audio;
    lector.audio_fallo = 0;
    lector.errores = errores;
    hilo = CreateThread(NULL, 0, hilo_lector, &lector, 0, NULL);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &codigo);
    if (hilo) {
        if (WaitForSingleObject(hilo, 1500) == WAIT_TIMEOUT) {
            CancelSynchronousIo(hilo);
            WaitForSingleObject(hilo, 1000);
        }
        CloseHandle(hilo);
    }
    CloseHandle(lectura);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    log_escribir(L"scrcpy terminó con código %lu", codigo);
    if (errores[0])
        MultiByteToWideChar(CP_UTF8, 0, errores, -1, ultimo_error, (int)cap);
    return codigo;
}
