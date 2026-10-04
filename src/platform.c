/* Fonctions dépendantes du système (macOS, Linux, Windows), déclarées dans util.h / settings.h.
 * Ce fichier n'inclut pas raylib : windows.h et raylib.h ne peuvent pas cohabiter. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "util.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <shlobj.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#endif
#ifdef __linux__
#include <limits.h>
#endif

/* ---------- dossiers et fichiers ---------- */

int mkdir_one(const char *path) {
#ifdef _WIN32
    return _mkdir(path) == 0 || errno == EEXIST ? 0 : -1;
#else
    return mkdir(path, 0755) == 0 || errno == EEXIST ? 0 : -1;
#endif
}

int move_file(const char *src, const char *dst) {
#ifdef _WIN32
    /* rename() refuse d'écraser un fichier existant sous Windows */
    return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) ? 0 : -1;
#else
    return rename(src, dst);
#endif
}

void file_private(const char *path) {
#ifdef _WIN32
    (void)path; /* le dossier %APPDATA% est déjà propre à l'utilisateur */
#else
    chmod(path, 0600);
#endif
}

void file_executable(const char *path) {
#ifdef _WIN32
    (void)path;
#else
    chmod(path, 0755);
#endif
}

const char *platform_data_root(void) {
    static char *root = NULL;
    if (root) return root;
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    root = xasprintf("%s/StrokaLauncher", appdata && *appdata ? appdata : ".");
    for (char *p = root; *p; p++)
        if (*p == '\\') *p = '/';
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    root = xasprintf("%s/Library/Application Support/StrokaLauncher", home ? home : ".");
#else
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) root = xasprintf("%s/StrokaLauncher", xdg);
    else root = xasprintf("%s/.local/share/StrokaLauncher", home ? home : ".");
#endif
    return root;
}

/* ---------- temps ---------- */

void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
#endif
}

long long mono_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/* ---------- processus ---------- */

#ifdef _WIN32
/* Règles de découpage de la ligne de commande Windows (CommandLineToArgvW) */
static void append_quoted(sbuf *b, const char *arg) {
    if (*arg && !strpbrk(arg, " \t\n\v\"")) {
        sb_add(b, arg);
        return;
    }
    sb_add(b, "\"");
    for (const char *p = arg;; p++) {
        int bs = 0;
        while (*p == '\\') {
            p++;
            bs++;
        }
        if (!*p) {
            for (int i = 0; i < bs * 2; i++) sb_add(b, "\\");
            break;
        }
        if (*p == '"') {
            for (int i = 0; i < bs * 2 + 1; i++) sb_add(b, "\\");
            sb_add(b, "\"");
        } else {
            for (int i = 0; i < bs; i++) sb_add(b, "\\");
            sb_addn(b, p, 1);
        }
    }
    sb_add(b, "\"");
}
#endif

int run_process(char *const argv[], const char *cwd) {
    fflush(stdout);
    fflush(stderr);
#ifdef _WIN32
    sbuf cmd;
    sb_init(&cmd);
    for (int i = 0; argv[i]; i++) {
        if (i) sb_add(&cmd, " ");
        append_quoted(&cmd, argv[i]);
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    /* pas de fenêtre de console pour Java / les outils */
    BOOL ok = CreateProcessA(NULL, cmd.s, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi);
    sb_free(&cmd);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (cwd && chdir(cwd) != 0) _exit(127);
        execvp(argv[0], argv);
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

int run_process_log(char *const argv[], const char *cwd, const char *log_path) {
    if (!log_path) return run_process(argv, cwd);
    mkdirs_parent(log_path);
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE}; /* poignée héritée par le jeu */
    HANDLE out = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) return run_process(argv, cwd);
    sbuf cmd;
    sb_init(&cmd);
    for (int i = 0; argv[i]; i++) {
        if (i) sb_add(&cmd, " ");
        append_quoted(&cmd, argv[i]);
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out;
    si.hStdError = out;
    memset(&pi, 0, sizeof pi);
    BOOL ok = CreateProcessA(NULL, cmd.s, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi);
    sb_free(&cmd);
    CloseHandle(out);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
#else
    FILE *f = fopen(log_path, "w");
    if (!f) return run_process(argv, cwd);
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        fclose(f);
        return -1;
    }
    if (pid == 0) {
        dup2(fileno(f), 1);
        dup2(fileno(f), 2);
        if (cwd && chdir(cwd) != 0) _exit(127);
        execvp(argv[0], argv);
        _exit(127);
    }
    fclose(f);
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
#endif
}

/* Chemin de l'exécutable en cours (séparateurs « / »), à libérer ; NULL si inconnu */
char *self_exe_path(void) {
#ifdef _WIN32
    char buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
    if (n == 0 || n >= sizeof buf) return NULL;
    for (char *p = buf; *p; p++)
        if (*p == '\\') *p = '/';
    return xstrdup(buf);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0) return NULL;
    char real[4096];
    return xstrdup(realpath(buf, real) ? real : buf);
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return NULL;
    buf[n] = '\0';
    return xstrdup(buf);
#endif
}

/* Lance un programme sans attendre sa fin (redémarrage après une mise à jour) */
int spawn_detached(char *const argv[]) {
#ifdef _WIN32
    sbuf cmd;
    sb_init(&cmd);
    for (int i = 0; argv[i]; i++) {
        if (i) sb_add(&cmd, " ");
        append_quoted(&cmd, argv[i]);
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    BOOL ok = CreateProcessA(NULL, cmd.s, NULL, NULL, FALSE, DETACHED_PROCESS, NULL, NULL, &si, &pi);
    sb_free(&cmd);
    if (!ok) return -1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setsid();
        execvp(argv[0], argv);
        _exit(127);
    }
    return 0;
#endif
}

void sys_open(const char *target) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", target, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    char *argv[] = {"open", (char *)target, NULL};
    run_process(argv, NULL);
#else
    char *argv[] = {"xdg-open", (char *)target, NULL};
    run_process(argv, NULL);
#endif
}

/* ---------- mémoire ---------- */

int system_ram_mb(void) {
#ifdef _WIN32
    MEMORYSTATUSEX m;
    m.dwLength = sizeof m;
    if (GlobalMemoryStatusEx(&m)) return (int)(m.ullTotalPhys / (1024 * 1024));
    return 8192;
#elif defined(__APPLE__)
    unsigned long long mem = 0;
    size_t len = sizeof mem;
    if (sysctlbyname("hw.memsize", &mem, &len, NULL, 0) == 0) return (int)(mem / (1024 * 1024));
    return 8192;
#else
    long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && size > 0) return (int)((unsigned long long)pages * (unsigned long long)size / (1024 * 1024));
    return 8192;
#endif
}

/* ---------- sélection de fichiers ---------- */

#ifndef _WIN32
/* Première ligne de la sortie d'une commande ; *missing = 1 si la commande n'existe pas */
static char *read_command(const char *cmd, int *missing) {
    if (missing) *missing = 0;
    FILE *f = popen(cmd, "r");
    if (!f) return NULL;
    char buf[4096] = "";
    char *got = fgets(buf, sizeof buf, f);
    int rc = pclose(f);
    if (missing && WIFEXITED(rc) && WEXITSTATUS(rc) == 127) *missing = 1;
    if (!got || rc != 0) return NULL;
    buf[strcspn(buf, "\r\n")] = '\0';
    size_t n = strlen(buf);
    if (n > 1 && buf[n - 1] == '/') buf[n - 1] = '\0';
    return buf[0] ? xstrdup(buf) : NULL;
}

/* Titre sans guillemets (inséré dans une commande) */
static void safe_prompt(const char *in, char *out, size_t n) {
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < n; p++) out[o++] = (*p == '"' || *p == '\'' || *p == '\\' || *p == '`' || *p == '$') ? ' ' : *p;
    out[o] = '\0';
}
#endif

#ifndef _WIN32
/* Toutes les lignes de la sortie d'une commande (chemins choisis), ajoutées à out */
static int read_command_lines(const char *cmd, char sep, strvec *out, int *missing) {
    if (missing) *missing = 0;
    FILE *f = popen(cmd, "r");
    if (!f) return 0;
    sbuf b;
    sb_init(&b);
    char buf[4096];
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) sb_addn(&b, buf, r);
    int rc = pclose(f);
    if (missing && WIFEXITED(rc) && WEXITSTATUS(rc) == 127) *missing = 1;
    int n = 0;
    if (rc == 0 && b.s) {
        for (char *line = b.s; line && *line;) {
            char *end = strchr(line, sep);
            if (!end) end = strchr(line, '\n');
            if (end) *end = '\0';
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0]) {
                sv_push(out, line);
                n++;
            }
            line = end ? end + 1 : NULL;
        }
    }
    sb_free(&b);
    return n;
}
#endif

int sys_pick_multi(int kind, const char *prompt, strvec *out) {
#ifdef _WIN32
    size_t cap = 1 << 16;
    char *buf = calloc(1, cap);
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = (DWORD)cap;
    ofn.lpstrTitle = prompt;
    ofn.lpstrFilter = kind == PICK_JAR     ? "Mods (*.jar)\0*.jar\0"
                      : kind == PICK_AUDIO ? "Musique (*.ogg;*.mp3;*.wav)\0*.ogg;*.mp3;*.wav\0"
                                           : "Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    int n = 0;
    if (GetOpenFileNameA(&ofn)) {
        /* un fichier : chemin complet ; plusieurs : dossier\0fichier1\0fichier2\0\0 */
        const char *dir = buf, *p = buf + strlen(buf) + 1;
        if (!*p) {
            sv_push(out, dir);
            n = 1;
        } else {
            for (; *p; p += strlen(p) + 1) {
                sv_push_owned(out, xasprintf("%s\\%s", dir, p));
                n++;
            }
        }
        for (size_t i = out->n - (size_t)n; i < out->n; i++)
            for (char *c = out->v[i]; *c; c++)
                if (*c == '\\') *c = '/';
    }
    free(buf);
    return n;
#else
    char title[256];
    safe_prompt(prompt, title, sizeof title);
    char *cmd;
    int n;
#ifdef __APPLE__
    const char *types = kind == PICK_JAR ? "{\"jar\", \"public.data\"}" : kind == PICK_AUDIO ? "{\"ogg\", \"mp3\", \"wav\", \"public.audio\"}"
                                                                                              : "{\"png\", \"jpg\", \"jpeg\", \"public.png\", \"public.jpeg\"}";
    cmd = xasprintf("osascript -e 'set f to choose file with prompt \"%s\" of type %s with multiple selections allowed' "
                    "-e 'set o to \"\"' -e 'repeat with x in f' -e 'set o to o & POSIX path of x & linefeed' -e 'end repeat' "
                    "-e 'return o' 2>/dev/null",
                    title, types);
    n = read_command_lines(cmd, '\n', out, NULL);
    free(cmd);
    return n;
#else
    const char *zf = kind == PICK_JAR ? "--file-filter='*.jar'" : kind == PICK_AUDIO ? "--file-filter='*.ogg *.mp3 *.wav'" : "--file-filter='*.png *.jpg *.jpeg'";
    int missing;
    cmd = xasprintf("zenity --file-selection --multiple --separator='|' --title=\"%s\" %s 2>/dev/null", title, zf);
    n = read_command_lines(cmd, '|', out, &missing);
    free(cmd);
    if (n || !missing) return n;
    cmd = xasprintf("kdialog --title \"%s\" --multiple --separate-output --getopenfilename . '%s' 2>/dev/null", title,
                    kind == PICK_JAR ? "*.jar" : kind == PICK_AUDIO ? "*.ogg *.mp3 *.wav" : "*.png *.jpg *.jpeg");
    n = read_command_lines(cmd, '\n', out, NULL);
    free(cmd);
    return n;
#endif
#endif
}

char *sys_pick(int kind, const char *prompt) {
#ifdef _WIN32
    char path[MAX_PATH * 4] = "";
    if (kind == PICK_FOLDER) {
        BROWSEINFOA bi;
        memset(&bi, 0, sizeof bi);
        bi.lpszTitle = prompt;
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST id = SHBrowseForFolderA(&bi);
        if (!id) return NULL;
        int ok = SHGetPathFromIDListA(id, path);
        CoTaskMemFree(id);
        if (!ok) return NULL;
    } else {
        OPENFILENAMEA ofn;
        memset(&ofn, 0, sizeof ofn);
        ofn.lStructSize = sizeof ofn;
        ofn.lpstrFile = path;
        ofn.nMaxFile = sizeof path;
        ofn.lpstrTitle = prompt;
        ofn.lpstrFilter = kind == PICK_JAR     ? "Mods (*.jar)\0*.jar\0"
                          : kind == PICK_AUDIO ? "Musique (*.ogg;*.mp3;*.wav)\0*.ogg;*.mp3;*.wav\0"
                                               : "Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameA(&ofn)) return NULL;
    }
    for (char *p = path; *p; p++)
        if (*p == '\\') *p = '/';
    return path[0] ? xstrdup(path) : NULL;
#else
    char title[256];
    safe_prompt(prompt, title, sizeof title);
    char *cmd, *r;
#ifdef __APPLE__
    if (kind == PICK_FOLDER) cmd = xasprintf("osascript -e 'POSIX path of (choose folder with prompt \"%s\")' 2>/dev/null", title);
    else if (kind == PICK_JAR)
        cmd = xasprintf("osascript -e 'POSIX path of (choose file with prompt \"%s\" of type {\"jar\", \"public.data\"})' 2>/dev/null", title);
    else if (kind == PICK_AUDIO)
        cmd = xasprintf("osascript -e 'POSIX path of (choose file with prompt \"%s\" of type {\"ogg\", \"mp3\", \"wav\", "
                        "\"public.audio\"})' 2>/dev/null",
                        title);
    else
        cmd = xasprintf("osascript -e 'POSIX path of (choose file of type {\"png\", \"jpg\", \"jpeg\", \"public.png\", \"public.jpeg\"} "
                        "with prompt \"%s\")' 2>/dev/null",
                        title);
    r = read_command(cmd, NULL);
    free(cmd);
    return r;
#else
    /* zenity (GNOME, Ubuntu), sinon kdialog (KDE : SteamOS, Kubuntu…) s'il n'est pas installé */
    const char *zf = kind == PICK_FOLDER  ? "--directory"
                     : kind == PICK_JAR   ? "--file-filter='*.jar'"
                     : kind == PICK_AUDIO ? "--file-filter='*.ogg *.mp3 *.wav'"
                                          : "--file-filter='*.png *.jpg *.jpeg'";
    int missing;
    cmd = xasprintf("zenity --file-selection --title=\"%s\" %s 2>/dev/null", title, zf);
    r = read_command(cmd, &missing);
    free(cmd);
    if (r || !missing) return r; /* choisi, ou annulé par le joueur */
    if (kind == PICK_FOLDER) cmd = xasprintf("kdialog --title \"%s\" --getexistingdirectory . 2>/dev/null", title);
    else
        cmd = xasprintf("kdialog --title \"%s\" --getopenfilename . '%s' 2>/dev/null", title,
                        kind == PICK_JAR ? "*.jar" : kind == PICK_AUDIO ? "*.ogg *.mp3 *.wav" : "*.png *.jpg *.jpeg");
    r = read_command(cmd, NULL);
    free(cmd);
    return r;
#endif
#endif
}
