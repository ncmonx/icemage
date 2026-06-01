/* v1.13.0: icmg.exe — IPC client + launcher hybrid.
 *
 * PRIMARY PATH (resident service alive):
 *   1. Connect to \\.\pipe\icmg-exec-<USERNAME>
 *   2. Frame argv + cwd + env → send request
 *   3. Stream response frames → write to stdout/stderr
 *   4. Exit with returned code
 *
 * FALLBACK PATH (no resident, cold start, or --ipc-bypass):
 *   Behave as v1.7-v1.12 launcher: sanitize_path, SetErrorMode,
 *   CreateProcess icmg-core.exe with same argv, wait, relay exit code.
 *
 * Imports: kernel32 + user32 only — no DLL search past system32, so
 * Win32 loader can't trigger B:/ popup. v1.7.0 launcher invariants preserved.
 *
 * Build: gcc -O2 -s exec_client.c -o icmg.exe -lkernel32 -luser32
 * Size target: <100KB.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- SetErrorMode (popup suppression, same as v1.7.0 launcher) ---------- */
static void set_safe_error_mode(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
}

static void attach_parent_console(void) {
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

static void sanitize_path(void) {
    char* path = getenv("PATH");
    if (!path) return;
    DWORD drives = GetLogicalDrives();
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

/* ---- Locate icmg-core.exe sibling --------------------------------------- */
static int locate_core(char* out, size_t outsz) {
    char self[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 1;
    char* last_sep = NULL;
    for (char* p = self; *p; ++p) {
        if (*p == '\\' || *p == '/') last_sep = p;
    }
    if (!last_sep) return 2;
    *last_sep = '\0';
    int r = _snprintf_s(out, outsz, _TRUNCATE,
                       "%s\\icmg-core.exe", self);
    if (r < 0 || (size_t)r >= outsz) return 3;
    return 0;
}

/* ---- JSON helpers (manual; avoid third-party for tiny client) ----------- */
static void json_escape_append(char* dst, size_t* di, size_t cap,
                               const char* s) {
    while (*s && *di + 2 < cap) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  if (*di + 2 < cap) { dst[(*di)++]='\\'; dst[(*di)++]='"';  } break;
            case '\\': if (*di + 2 < cap) { dst[(*di)++]='\\'; dst[(*di)++]='\\'; } break;
            case '\n': if (*di + 2 < cap) { dst[(*di)++]='\\'; dst[(*di)++]='n';  } break;
            case '\r': if (*di + 2 < cap) { dst[(*di)++]='\\'; dst[(*di)++]='r';  } break;
            case '\t': if (*di + 2 < cap) { dst[(*di)++]='\\'; dst[(*di)++]='t';  } break;
            default:
                if (c < 0x20) {
                    if (*di + 6 < cap) {
                        _snprintf_s(dst + *di, cap - *di, _TRUNCATE,
                                    "\\u%04x", c);
                        *di += 6;
                    }
                } else {
                    dst[(*di)++] = (char)c;
                }
        }
        ++s;
    }
}

/* Read all of stdin if it's a pipe/file (not console). Returns malloc'd buf. */
static char* slurp_stdin(size_t* out_len) {
    *out_len = 0;
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD ftype = GetFileType(h);
    if (ftype == FILE_TYPE_CHAR) return NULL;  /* console = no stdin data */
    char* buf = (char*)malloc(65536);
    if (!buf) return NULL;
    size_t cap = 65536, len = 0;
    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            if (cap > 4 * 1024 * 1024) break;  /* 4MB hard cap */
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        DWORD n = 0;
        if (!ReadFile(h, buf + len, (DWORD)(cap - len), &n, NULL) || n == 0) break;
        len += n;
    }
    *out_len = len;
    return buf;
}

/* Build {"op":"exec","argv":[...],"cwd":"...","stdin":"..."} into buf. */
static size_t build_request(char* buf, size_t cap, int argc, char** argv,
                             const char* stdin_buf, size_t stdin_len) {
    size_t i = 0;
    if (cap < 64) return 0;
    const char* hdr = "{\"op\":\"exec\",\"argv\":[";
    size_t hl = strlen(hdr);
    memcpy(buf + i, hdr, hl); i += hl;
    for (int k = 1; k < argc; ++k) {
        if (k > 1 && i + 1 < cap) buf[i++] = ',';
        if (i + 1 < cap) buf[i++] = '"';
        json_escape_append(buf, &i, cap, argv[k]);
        if (i + 1 < cap) buf[i++] = '"';
    }
    if (i + 10 < cap) {
        memcpy(buf + i, "],\"cwd\":\"", 9); i += 9;
    }
    char cwd[MAX_PATH];
    DWORD cwdn = GetCurrentDirectoryA(MAX_PATH, cwd);
    if (cwdn > 0 && cwdn < MAX_PATH) {
        json_escape_append(buf, &i, cap, cwd);
    }
    if (i + 11 < cap) { memcpy(buf + i, "\",\"stdin\":\"", 11); i += 11; }
    if (stdin_buf && stdin_len > 0) {
        /* Append as JSON string body. Cap-aware. */
        for (size_t k = 0; k < stdin_len && i + 6 < cap; ++k) {
            unsigned char c = (unsigned char)stdin_buf[k];
            switch (c) {
                case '"':  buf[i++]='\\'; buf[i++]='"';  break;
                case '\\': buf[i++]='\\'; buf[i++]='\\'; break;
                case '\n': buf[i++]='\\'; buf[i++]='n';  break;
                case '\r': buf[i++]='\\'; buf[i++]='r';  break;
                case '\t': buf[i++]='\\'; buf[i++]='t';  break;
                default:
                    if (c < 0x20) {
                        _snprintf_s(buf + i, cap - i, _TRUNCATE, "\\u%04x", c);
                        i += 6;
                    } else {
                        buf[i++] = (char)c;
                    }
            }
        }
    }
    if (i + 2 < cap) { buf[i++] = '"'; buf[i++] = '}'; }
    buf[i] = '\0';
    return i;
}

/* ---- IPC try-route: connect, send, stream response, exit code ----------- */
static int try_ipc(int argc, char** argv) {
    const char* user = getenv("USERNAME");
    char pipe_name[256];
    if (user && *user) {
        _snprintf_s(pipe_name, sizeof(pipe_name), _TRUNCATE,
                    "\\\\.\\pipe\\icmg-exec-%s", user);
    } else {
        strcpy_s(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\icmg-exec");
    }

    HANDLE h = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;  /* not running */

    /* Slurp stdin (if pipe/file). */
    size_t stdin_len = 0;
    char* stdin_buf = slurp_stdin(&stdin_len);

    /* Build request payload. Allocate enough for stdin. */
    size_t cap = 16384 + stdin_len * 6 + 1024;  /* worst-case JSON escape */
    char* req = (char*)malloc(cap);
    if (!req) { free(stdin_buf); CloseHandle(h); return -1; }
    size_t req_len = build_request(req, cap, argc, argv, stdin_buf, stdin_len);
    free(stdin_buf);
    if (req_len == 0) { free(req); CloseHandle(h); return -1; }

    /* Length prefix (LE uint32). */
    DWORD wrote = 0;
    unsigned char prefix[4] = {
        (unsigned char)(req_len & 0xff),
        (unsigned char)((req_len >> 8) & 0xff),
        (unsigned char)((req_len >> 16) & 0xff),
        (unsigned char)((req_len >> 24) & 0xff),
    };
    if (!WriteFile(h, prefix, 4, &wrote, NULL) || wrote != 4) {
        free(req); CloseHandle(h); return -1;
    }
    if (!WriteFile(h, req, (DWORD)req_len, &wrote, NULL) || wrote != req_len) {
        free(req); CloseHandle(h); return -1;
    }
    free(req);

    /* Read streamed response frames. */
    int exit_code = 0;
    for (;;) {
        unsigned char len_buf[4];
        DWORD got = 0;
        if (!ReadFile(h, len_buf, 4, &got, NULL) || got != 4) break;
        DWORD n = ((DWORD)len_buf[0]) | ((DWORD)len_buf[1] << 8)
                | ((DWORD)len_buf[2] << 16) | ((DWORD)len_buf[3] << 24);
        if (n == 0 || n > 16 * 1024 * 1024) break;
        char* payload = (char*)malloc(n + 1);
        if (!payload) break;
        DWORD total = 0;
        while (total < n) {
            DWORD chunk = 0;
            if (!ReadFile(h, payload + total, n - total, &chunk, NULL)
                || chunk == 0) { free(payload); CloseHandle(h); return -1; }
            total += chunk;
        }
        payload[n] = '\0';

        /* Cheap JSON keyword detect. */
        if (strstr(payload, "\"done\"")) {
            const char* p = strstr(payload, "\"exit\":");
            if (p) exit_code = atoi(p + 7);
            free(payload);
            break;
        }
        /* out / err: extract value between quoted "out": or "err": */
        const char* tag = NULL;
        FILE* dst = stdout;
        if ((tag = strstr(payload, "\"out\":\""))) { dst = stdout; tag += 7; }
        else if ((tag = strstr(payload, "\"err\":\""))) { dst = stderr; tag += 7; }
        if (tag) {
            /* unescape until closing quote (basic JSON). */
            const char* p = tag;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) {
                    char e = p[1];
                    if      (e == 'n')  fputc('\n', dst);
                    else if (e == 'r')  fputc('\r', dst);
                    else if (e == 't')  fputc('\t', dst);
                    else if (e == '"')  fputc('"',  dst);
                    else if (e == '\\') fputc('\\', dst);
                    else if (e == '/')  fputc('/',  dst);
                    else                fputc(e,    dst);
                    p += 2;
                } else {
                    fputc(*p, dst);
                    ++p;
                }
            }
            fflush(dst);
        }
        free(payload);
    }

    CloseHandle(h);
    return exit_code;
}

/* ---- Argv quoting for fallback CreateProcess ---------------------------- */
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

/* ---- Fallback: spawn icmg-core.exe directly ----------------------------- */
static int fallback_spawn(int argc, char** argv) {
    char core_path[MAX_PATH];
    if (locate_core(core_path, sizeof(core_path)) != 0) {
        fprintf(stderr, "icmg: cannot locate icmg-core.exe\n");
        return 127;
    }
    size_t cmdlen = strlen(core_path) + 3;
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
    BOOL ok = CreateProcessA(core_path, cmd, NULL, NULL, TRUE, 0,
                              NULL, NULL, &si, &pi);
    free(cmd);
    if (!ok) {
        fprintf(stderr, "icmg: spawn failed (err=%lu)\n",
                (unsigned long)GetLastError());
        return 127;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    return (int)exit_code;
}

/* ---- Cmds that must always spawn direct (not IPC) ---------------------- */
static int needs_direct_spawn(int argc, char** argv) {
    if (argc < 2) return 1;  /* no args → spawn for default help */
    const char* cmd = argv[1];
    /* Top-level flags handled in main.cpp, not Registry. */
    if (cmd[0] == '-') return 1;
    /* Long-running / interactive / stdio-bound. */
    if (strcmp(cmd, "service") == 0)   return 1;  /* service run is the server */
    if (strcmp(cmd, "chat") == 0)      return 1;  /* interactive TTY */
    if (strcmp(cmd, "serve") == 0)     return 1;  /* long-running HTTP */
    if (strcmp(cmd, "--mcp-server") == 0) return 1;
    if (strcmp(cmd, "daemon") == 0)    return 1;
    if (strcmp(cmd, "rule-daemon") == 0) return 1;
    /* Bypass flag. */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--ipc-bypass") == 0) return 1;
    }
    /* ICMG_IPC=0 env override. */
    const char* off = getenv("ICMG_IPC");
    if (off && (off[0] == '0' || off[0] == 'n' || off[0] == 'N')) return 1;
    return 0;
}

/* v1.18.0: auto-spawn icmg-core service if dead. Called when IPC pipe
 * unavailable AND we want background service alive for popup-killer +
 * future invocations. Reads ~/.icmg/service.pid, checks PID alive, and
 * if dead → spawns `icmg-core.exe service run` detached. Best-effort. */
static void maybe_autospawn_service(void) {
    /* v1.18.1: opt-out via env (used by update --apply fan-out to prevent
     * cascade spawn while loop iterates registered projects). */
    if (getenv("ICMG_NO_AUTOSPAWN")) return;

    const char* user = getenv("USERPROFILE");
    if (!user || !*user) return;

    /* v1.21.5: hot-reload-friendly update — `update --apply` writes
     * ~/.icmg/updating.lock before binary swap. While present, any
     * auto-spawn paths skip launching icmg-core so the swap can complete
     * without racing against a new service starting on the old binary. */
    {
        char update_lock[MAX_PATH];
        _snprintf_s(update_lock, sizeof(update_lock), _TRUNCATE,
                    "%s\\.icmg\\updating.lock", user);
        DWORD attrs = GetFileAttributesA(update_lock);
        if (attrs != INVALID_FILE_ATTRIBUTES) return;
    }

    /* v1.18.1: PRE-CHECK singleton mutex via OpenMutexA. If mutex exists
     * → service starting OR alive → skip spawn. Prevents N-client race
     * where each thinks service dead + all spawn simultaneously. */
    {
        const char* username = getenv("USERNAME");
        if (username && *username) {
            char mtx_name[256];
            _snprintf_s(mtx_name, sizeof(mtx_name), _TRUNCATE,
                        "Global\\icmg-service-%s", username);
            HANDLE existing = OpenMutexA(SYNCHRONIZE, FALSE, mtx_name);
            if (existing) {
                CloseHandle(existing);
                return;  /* service already running or being started */
            }
        }
    }

    /* Read pid file as belt-and-suspenders. */
    char pidfile[MAX_PATH];
    _snprintf_s(pidfile, sizeof(pidfile), _TRUNCATE,
                "%s\\.icmg\\service.pid", user);
    FILE* pf = NULL;
    fopen_s(&pf, pidfile, "r");
    int alive = 0;
    if (pf) {
        long pid = 0;
        if (fscanf_s(pf, "%ld", &pid) == 1 && pid > 0) {
            HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                    FALSE, (DWORD)pid);
            if (ph) {
                DWORD ec = 0;
                if (GetExitCodeProcess(ph, &ec) && ec == STILL_ACTIVE) {
                    alive = 1;
                }
                CloseHandle(ph);
            }
        }
        fclose(pf);
    }
    if (alive) return;

    /* v1.18.1: starting-sentinel — atomic CreateFile EXCLUSIVE. If sentinel
     * exists, another client is mid-spawn → skip. Self-delete after spawn. */
    char sentinel[MAX_PATH];
    _snprintf_s(sentinel, sizeof(sentinel), _TRUNCATE,
                "%s\\.icmg\\service.starting", user);
    HANDLE sh = CreateFileA(sentinel, GENERIC_WRITE, 0, NULL,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (sh == INVALID_HANDLE_VALUE) {
        /* Sentinel exists → another instance mid-spawn. Skip. */
        return;
    }
    /* Hold handle until spawn done (auto-deletes via FILE_FLAG_DELETE_ON_CLOSE
     * not used; manual cleanup at end). */

    /* Spawn icmg-core service run detached. */
    char core_path[MAX_PATH];
    if (locate_core(core_path, sizeof(core_path)) != 0) {
        CloseHandle(sh);
        DeleteFileA(sentinel);
        return;
    }
    char cmd[MAX_PATH + 32];
    _snprintf_s(cmd, sizeof(cmd), _TRUNCATE,
                "\"%s\" service run", core_path);

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    BOOL ok = CreateProcessA(core_path, cmd, NULL, NULL, FALSE,
                              CREATE_NO_WINDOW | DETACHED_PROCESS
                              | CREATE_NEW_PROCESS_GROUP,
                              NULL, NULL, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(sh);
    DeleteFileA(sentinel);
}

int main(int argc, char* argv[]) {
    set_safe_error_mode();
    attach_parent_console();
    sanitize_path();

    /* v1.20.2: multi-user safety — ICMG_NO_IPC=1 opts out of IPC + autospawn
     * entirely. Useful when multiple Windows users share a single icmg.exe
     * install: each user's exec_client should NOT try connecting to another
     * user's icmg-service pipe and should NOT spawn a service of its own. */
    int no_ipc = (getenv("ICMG_NO_IPC") != NULL);

    if (!no_ipc && !needs_direct_spawn(argc, argv)) {
        int rc = try_ipc(argc, argv);
        if (rc >= 0) return rc;
        /* IPC unavailable → service may be dead. Auto-spawn it for
         * future invocations (popup-killer thread etc.), then fall back
         * to direct spawn for THIS invocation. */
        maybe_autospawn_service();
    }
    return fallback_spawn(argc, argv);
}
