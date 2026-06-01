/* v1.7.0: icmg-launcher.exe — minimal stub.
 *
 * PROBLEM: full icmg.exe imports onnxruntime.dll, libtree-sitter, libzstd,
 * wasmtime, libwinpthread. Win32 DLL loader resolves imports BEFORE main()
 * runs. Loader searches multiple dirs including every PATH entry. If user's
 * PATH contains entries pointing to non-existent drives (e.g. legacy /b/foo
 * → B:\foo via MSYS translation), each probe triggers the "B:/ — system
 * cannot find drive specified" popup. SetErrorMode in main() is too late.
 *
 * SOLUTION: tiny stub with ONLY kernel32 + user32 imports. No DLL search
 * past system32. Stub:
 *   1. SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX) —
 *      now inherited by child icmg-core.exe before its imports resolve.
 *   2. Strip B:/b:/B/ entries from PATH env (best-effort defensive).
 *   3. Locate icmg-core.exe (alongside icmg.exe).
 *   4. CreateProcess core with same argv, stdio inherited, wait, exit code
 *      passed through.
 *
 * Build: gcc -O2 -s -mconsole=no -mwindows-NO -Wl,--subsystem,windows
 *        launcher.c -o icmg-launcher.exe -lkernel32 -luser32
 *
 * Size target: <80KB.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* SetErrorMode + AttachConsole — set FIRST, before any other Win32 call
 * that might touch a file path. */
static void set_safe_error_mode(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
}

/* Attach to parent's console (cmd/PowerShell/bash terminal) so child's
 * stdio reaches user's terminal. Fail-silent on headless invocations. */
static void attach_parent_console(void) {
    /* Sniff parent stdio first; if inherited (bash pipes etc), don't
     * replace it. */
    HANDLE h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE h_in  = GetStdHandle(STD_INPUT_HANDLE);
    int inherited_out = (h_out && h_out != INVALID_HANDLE_VALUE);
    int inherited_in  = (h_in  && h_in  != INVALID_HANDLE_VALUE);
    BOOL ok = AttachConsole(ATTACH_PARENT_PROCESS);
    if (ok && !inherited_out) {
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
    }
    if (ok && !inherited_in) {
        FILE* fp;
        freopen_s(&fp, "CONIN$", "r", stdin);
    }
}

/* Strip PATH entries that map to non-existent drives. MSYS path-conv style
 * `/b/foo` becomes Win32 `B:\foo`. Skip any entry whose first char is a
 * letter not present in GetLogicalDrives(). Defensive — even if loader
 * usually skips empty entries, we proactively remove them. */
static void sanitize_path(void) {
    char* path = getenv("PATH");
    if (!path) return;
    DWORD drives = GetLogicalDrives();  /* bitmask A-Z */
    size_t len = strlen(path);
    char* out = (char*)malloc(len + 1);
    if (!out) return;
    size_t oi = 0;
    const char* start = path;
    for (const char* p = path; ; ++p) {
        if (*p == ';' || *p == '\0') {
            size_t seg_len = (size_t)(p - start);
            int skip = 0;
            if (seg_len >= 2 && start[1] == ':') {
                char drv = start[0];
                if (drv >= 'a' && drv <= 'z') drv = (char)(drv - 32);
                if (drv >= 'A' && drv <= 'Z') {
                    int bit = drv - 'A';
                    if (!((drives >> bit) & 1)) skip = 1;
                }
            }
            if (!skip && seg_len > 0) {
                if (oi > 0) out[oi++] = ';';
                memcpy(out + oi, start, seg_len);
                oi += seg_len;
            }
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    out[oi] = '\0';
    SetEnvironmentVariableA("PATH", out);
    free(out);
}

/* Build path to icmg-core.exe sibling of icmg-launcher.exe. Returns 0 on
 * success, non-zero on failure. */
static int locate_core(char* out, size_t outsz) {
    char self[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 1;
    /* Strip filename → keep directory. */
    char* last_sep = NULL;
    for (char* p = self; *p; ++p) {
        if (*p == '\\' || *p == '/') last_sep = p;
    }
    if (!last_sep) return 2;
    *last_sep = '\0';
    int r = snprintf(out, outsz, "%s\\icmg-core.exe", self);
    if (r < 0 || (size_t)r >= outsz) return 3;
    return 0;
}

/* Quote an argv element for CreateProcess cmdline. Returns malloc'd string. */
static char* quote_arg(const char* arg) {
    size_t len = strlen(arg);
    int needs_quote = (len == 0);
    for (size_t i = 0; i < len; ++i) {
        char c = arg[i];
        if (c == ' ' || c == '\t' || c == '"') { needs_quote = 1; break; }
    }
    if (!needs_quote) {
        char* o = (char*)malloc(len + 1);
        if (!o) return NULL;
        memcpy(o, arg, len + 1);
        return o;
    }
    /* Worst case: every char escaped + 2 quotes. */
    char* o = (char*)malloc(len * 2 + 3);
    if (!o) return NULL;
    size_t oi = 0;
    o[oi++] = '"';
    for (size_t i = 0; i < len; ++i) {
        if (arg[i] == '"') o[oi++] = '\\';
        o[oi++] = arg[i];
    }
    o[oi++] = '"';
    o[oi] = '\0';
    return o;
}

int main(int argc, char* argv[]) {
    set_safe_error_mode();
    attach_parent_console();
    sanitize_path();

    char core_path[MAX_PATH];
    if (locate_core(core_path, sizeof(core_path)) != 0) {
        fprintf(stderr, "icmg: locator failed to find icmg-core.exe\n");
        return 127;
    }

    /* Build cmdline: "core_path" arg1 arg2 ... */
    size_t cmdlen = strlen(core_path) + 3;  /* "..." + space */
    for (int i = 1; i < argc; ++i) cmdlen += strlen(argv[i]) * 2 + 4;
    char* cmd = (char*)malloc(cmdlen + 64);
    if (!cmd) return 127;
    size_t off = 0;
    char* q = quote_arg(core_path);
    if (!q) { free(cmd); return 127; }
    off += sprintf(cmd + off, "%s", q);
    free(q);
    for (int i = 1; i < argc; ++i) {
        q = quote_arg(argv[i]);
        if (!q) { free(cmd); return 127; }
        off += sprintf(cmd + off, " %s", q);
        free(q);
    }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    BOOL ok = CreateProcessA(core_path, cmd, NULL, NULL, TRUE,
                              0, /* inherit error mode */
                              NULL, NULL, &si, &pi);
    free(cmd);
    if (!ok) {
        DWORD err = GetLastError();
        fprintf(stderr, "icmg: cannot spawn icmg-core.exe (err=%lu)\n",
                (unsigned long)err);
        return 127;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    return (int)exit_code;
}
