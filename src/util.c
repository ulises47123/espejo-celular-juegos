/*
 * Utilidades: registro (log), archivos y ejecucion de procesos ocultos.
 */
#include "rtxjuegos.h"
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

static CRITICAL_SECTION s_log_cs;
static HANDLE s_log = INVALID_HANDLE_VALUE;

void log_iniciar(void)
{
    InitializeCriticalSection(&s_log_cs);
    /* Se reinicia en cada arranque para que no crezca sin limite */
    s_log = CreateFileW(g_rutas.log, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void log_crudo(const char *datos, DWORD n)
{
    DWORD escrito;
    if (s_log == INVALID_HANDLE_VALUE || n == 0)
        return;
    EnterCriticalSection(&s_log_cs);
    WriteFile(s_log, datos, n, &escrito, NULL);
    LeaveCriticalSection(&s_log_cs);
}

void log_escribir(const wchar_t *fmt, ...)
{
    wchar_t linea[2048];
    char utf8[6144];
    SYSTEMTIME t;
    va_list ap;
    int n, m;

    GetLocalTime(&t);
    n = swprintf(linea, 2048, L"[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    vswprintf(linea + n, 2048 - n - 2, fmt, ap);
    va_end(ap);
    wcscat(linea, L"\r\n");

    m = WideCharToMultiByte(CP_UTF8, 0, linea, -1, utf8, sizeof utf8, NULL, NULL);
    if (m > 1)
        log_crudo(utf8, (DWORD)(m - 1));
}

int archivo_existe(const wchar_t *ruta)
{
    DWORD a = GetFileAttributesW(ruta);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int carpeta_existe(const wchar_t *ruta)
{
    DWORD a = GetFileAttributesW(ruta);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

void ruta_unir(wchar_t *dest, size_t cap, const wchar_t *a, const wchar_t *b)
{
    size_t n = wcslen(a);
    swprintf(dest, cap, (n && a[n - 1] == L'\\') ? L"%ls%ls" : L"%ls\\%ls", a, b);
}

int borrar_carpeta(const wchar_t *ruta)
{
    wchar_t patron[MAX_PATH], hijo[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    ruta_unir(patron, MAX_PATH, ruta, L"*");
    h = FindFirstFileW(patron, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            ruta_unir(hijo, MAX_PATH, ruta, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                borrar_carpeta(hijo);
            } else {
                SetFileAttributesW(hijo, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(hijo);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(ruta) != 0;
}

void cmd_agregar(wchar_t *cmd, size_t cap, const wchar_t *fmt, ...)
{
    size_t n = wcslen(cmd);
    va_list ap;
    if (n + 2 >= cap)
        return;
    if (n > 0)
        cmd[n++] = L' ';
    va_start(ap, fmt);
    vswprintf(cmd + n, cap - n, fmt, ap);
    va_end(ap);
}

int ejecutar_capturando(const wchar_t *cmd, char *salida, size_t cap, DWORD espera_ms, DWORD *codigo)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE lectura, escritura;
    wchar_t *linea;
    size_t usado = 0;
    DWORD inicio, leido, disponible;
    char tmp[4096];

    if (salida && cap)
        salida[0] = 0;
    if (codigo)
        *codigo = (DWORD)-1;

    if (!CreatePipe(&lectura, &escritura, &sa, 0))
        return 0;
    SetHandleInformation(lectura, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = escritura;
    si.hStdError = escritura;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    linea = _wcsdup(cmd); /* CreateProcessW puede modificar el buffer */
    if (!CreateProcessW(NULL, linea, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        free(linea);
        CloseHandle(lectura);
        CloseHandle(escritura);
        return 0;
    }
    free(linea);
    CloseHandle(escritura);

    /* Lectura con limite de tiempo: si el proceso se cuelga no bloquea al programa */
    inicio = GetTickCount();
    for (;;) {
        if (!PeekNamedPipe(lectura, NULL, 0, NULL, &disponible, NULL))
            break; /* tuberia cerrada: termino */
        if (disponible) {
            if (!ReadFile(lectura, tmp, sizeof tmp, &leido, NULL) || leido == 0)
                break;
            if (salida && usado + 1 < cap) {
                size_t copiar = leido < cap - usado - 1 ? leido : cap - usado - 1;
                memcpy(salida + usado, tmp, copiar);
                usado += copiar;
                salida[usado] = 0;
            }
        } else {
            if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) {
                /* termino: vaciar lo que quede */
                while (PeekNamedPipe(lectura, NULL, 0, NULL, &disponible, NULL) && disponible &&
                       ReadFile(lectura, tmp, sizeof tmp, &leido, NULL) && leido) {
                    if (salida && usado + 1 < cap) {
                        size_t copiar = leido < cap - usado - 1 ? leido : cap - usado - 1;
                        memcpy(salida + usado, tmp, copiar);
                        usado += copiar;
                        salida[usado] = 0;
                    }
                }
                break;
            }
        }
        if (GetTickCount() - inicio > espera_ms) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    WaitForSingleObject(pi.hProcess, 2000);
    if (codigo)
        GetExitCodeProcess(pi.hProcess, codigo);
    CloseHandle(lectura);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 1;
}
