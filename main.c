/* VitaSwitch: guarded mode switching for PS Vita.
 * File/directory swaps are journaled and originals remain recoverable until
 * the state update is committed. No existing config is deleted to make way
 * for a replacement. See README.md for power-loss recovery limitations.
 */
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/power.h>
#include <stdio.h>
#include <string.h>

#define PATH_TAI "ur0:tai/"
#define FILE_CONFIG       PATH_TAI "config.txt"
#define FILE_CONFIG_PORT  PATH_TAI "config_portable.txt"
#define FILE_CONFIG_DOCK  PATH_TAI "config_docked.txt"
#define FILE_STATE        PATH_TAI "switchstate.txt"
#define FILE_SWITCHCONF   PATH_TAI "switchconf.txt"
#define FILE_JOURNAL      PATH_TAI "vitaswitch.transaction"
#define FILE_COMMIT       PATH_TAI "vitaswitch.committed"
#define FILE_COMMIT_TMP   PATH_TAI "vitaswitch.committed.tmp"
#define FILE_ERROR        PATH_TAI "vitaswitch-error.txt"
#define PATH_VITAGFX      "ux0:data/VitaGrafix/"
#define VG_CONFIG         PATH_VITAGFX "config.txt"
#define VG_CONFIG_PORT    PATH_VITAGFX "config_portable.txt"
#define VG_CONFIG_DOCK    PATH_VITAGFX "config_docked.txt"
#define PATH_PSVSHELL     "ur0:data/PSVshell/"
#define PSV_PROFILES      PATH_PSVSHELL "profiles"
#define PSV_PROFILES_PORT PATH_PSVSHELL "profiles_portable"
#define PSV_PROFILES_DOCK PATH_PSVSHELL "profiles_docked"
#define PATH_LIVEAREA     "ur0:appmeta/SWCH00001/livearea/contents/"
#define BG_LIVEAREA       PATH_LIVEAREA "bg.png"
#define BG_PORTABLE       PATH_LIVEAREA "bg_portable.png"
#define BG_DOCKED         PATH_LIVEAREA "bg_docked.png"
#define ICON_LIVEAREA     "ur0:appmeta/SWCH00001/icon0.png"
#define ICON_PORTABLE     PATH_LIVEAREA "icon_portable.png"
#define ICON_DOCKED       PATH_LIVEAREA "icon_docked.png"

#define PATH_CAP 512
#define MAX_TREE_DEPTH 16
#define FILE_PERMS 0666
#define DIR_PERMS 0777

typedef struct {
    const char *active;
    const char *portable;
    const char *docked;
    int is_dir;
    int required;
} SwitchPair;

static const SwitchPair pairs[] = {
    {FILE_CONFIG, FILE_CONFIG_PORT, FILE_CONFIG_DOCK, 0, 1},
    {PSV_PROFILES, PSV_PROFILES_PORT, PSV_PROFILES_DOCK, 1, 0},
    {VG_CONFIG, VG_CONFIG_PORT, VG_CONFIG_DOCK, 0, 0}
};
#define PAIR_COUNT (sizeof(pairs) / sizeof(pairs[0]))

static int path_stat(const char *p, SceIoStat *st) {
    return sceIoGetstat(p, st);
}
static int exists(const char *p) {
    SceIoStat st;
    return path_stat(p, &st) >= 0;
}
static int is_expected(const char *p, int dir) {
    SceIoStat st;
    if (path_stat(p, &st) < 0) return 0;
    return dir ? SCE_S_ISDIR(st.st_mode) : SCE_S_ISREG(st.st_mode);
}
static int suffix(char *buf, size_t cap, const char *path, const char *add) {
    int n = snprintf(buf, cap, "%s%s", path, add);
    return n >= 0 && (size_t)n < cap ? 0 : -1;
}
static int valid_name(const char *name) {
    const char *end = memchr(name, '\0', sizeof(((SceIoDirent *)0)->d_name));
    if (!end || end == name) return 0;
    for (const char *p = name; p < end; p++)
        if (*p == '/' || *p == '\\' || *p == ':') return 0;
    return 1;
}
static int child_path(char *buf, size_t cap, const char *dir, const char *name) {
    int n = snprintf(buf, cap, "%s/%s", dir, name);
    return n >= 0 && (size_t)n < cap ? 0 : -1;
}
/* Each mode has a parked backup. Save the outgoing active mode to its own
 * backup; read the incoming mode from the other backup. Both are retained.
 */
static const char *outgoing(const SwitchPair *p, int state) {
    return state ? p->docked : p->portable;
}
static const char *incoming(const SwitchPair *p, int state) {
    return state ? p->portable : p->docked;
}
static int write_all(SceUID fd, const void *data, SceSize len) {
    const char *buf = (const char *)data;
    SceSize off = 0;
    while (off < len) {
        SceSSize n = sceIoWrite(fd, buf + off, len - off);
        if (n <= 0 || (SceSize)n > len - off) return -1;
        off += (SceSize)n;
    }
    return 0;
}
static void report_error(const char *reason) {
    fprintf(stderr, "VitaSwitch: %s\n", reason);
    SceUID fd = sceIoOpen(FILE_ERROR, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, FILE_PERMS);
    if (fd >= 0) {
        (void)write_all(fd, reason, (SceSize)strlen(reason));
        (void)sceIoClose(fd);
    }
}

/* Fail closed on read errors, zero-length writes, partial writes and close errors. */
static int copy_file(const char *src, const char *dst) {
    SceUID in = sceIoOpen(src, SCE_O_RDONLY, 0);
    if (in < 0) return -1;
    SceUID out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, FILE_PERMS);
    if (out < 0) { (void)sceIoClose(in); return -1; }
    int ok = 1;
    char buf[4096];
    for (;;) {
        SceSSize n = sceIoRead(in, buf, sizeof(buf));
        if (n < 0) { ok = 0; break; }
        if (n == 0) break;
        SceSSize off = 0;
        while (off < n) {
            SceSSize written = sceIoWrite(out, buf + off, (SceSize)(n - off));
            if (written <= 0 || written > n - off) { ok = 0; break; }
            off += written;
        }
        if (!ok) break;
    }
    if (ok && sceIoSyncByFd(out, 0) < 0) ok = 0;
    if (sceIoClose(out) < 0) ok = 0;
    if (sceIoClose(in) < 0) ok = 0;
    if (ok) {
        SceIoStat a, b;
        if (path_stat(src, &a) < 0 || path_stat(dst, &b) < 0 || a.st_size != b.st_size)
            ok = 0;
    }
    if (!ok) (void)sceIoRemove(dst); /* only a newly created destination */
    return ok ? 0 : -1;
}

static int remove_path(const char *path, int depth);

static int copy_path(const char *src, const char *dst, int depth) {
    if (depth > MAX_TREE_DEPTH || exists(dst)) return -1;
    SceIoStat st;
    if (path_stat(src, &st) < 0) return -1;
    if (SCE_S_ISREG(st.st_mode)) return copy_file(src, dst);
    if (!SCE_S_ISDIR(st.st_mode) || sceIoMkdir(dst, DIR_PERMS) < 0) return -1;
    SceUID d = sceIoDopen(src);
    if (d < 0) { (void)sceIoRmdir(dst); return -1; }
    int ok = 1;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    int n;
    while ((n = sceIoDread(d, &ent)) > 0) {
        if (!valid_name(ent.d_name)) { ok = 0; break; }
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) continue;
        char s[PATH_CAP], t[PATH_CAP];
        if (child_path(s, sizeof(s), src, ent.d_name) ||
            child_path(t, sizeof(t), dst, ent.d_name) ||
            copy_path(s, t, depth + 1)) { ok = 0; break; }
        memset(&ent, 0, sizeof(ent));
    }
    if (n < 0) ok = 0;
    if (sceIoDclose(d) < 0) ok = 0;
    if (!ok) (void)remove_path(dst, depth);
    return ok ? 0 : -1;
}

/* Only call on VitaSwitch-owned scratch paths; never on a user's live backup. */
static int remove_path(const char *path, int depth) {
    if (depth > MAX_TREE_DEPTH) return -1;
    SceIoStat st;
    if (path_stat(path, &st) < 0) return 0;
    if (SCE_S_ISREG(st.st_mode)) return sceIoRemove(path) < 0 ? -1 : 0;
    if (!SCE_S_ISDIR(st.st_mode)) return -1;
    SceUID d = sceIoDopen(path);
    if (d < 0) return -1;
    int ok = 1, n;
    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));
    while ((n = sceIoDread(d, &ent)) > 0) {
        if (!valid_name(ent.d_name)) { ok = 0; break; }
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) continue;
        char child[PATH_CAP];
        if (child_path(child, sizeof(child), path, ent.d_name) ||
            remove_path(child, depth + 1)) { ok = 0; break; }
        memset(&ent, 0, sizeof(ent));
    }
    if (n < 0) ok = 0;
    if (sceIoDclose(d) < 0) ok = 0;
    if (!ok) return -1;
    return sceIoRmdir(path) < 0 ? -1 : 0;
}

static int read_digit(const char *path, int *state) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return -1;
    char c = 0, extra = 0;
    SceSSize n = sceIoRead(fd, &c, 1);
    SceSSize more = n == 1 ? sceIoRead(fd, &extra, 1) : -1;
    int closed = sceIoClose(fd);
    if (n != 1 || more != 0 || closed < 0 || (c != '0' && c != '1')) return -1;
    *state = c == '1';
    return 0;
}
static int write_digit(const char *path, int value, int exclusive) {
    int flags = SCE_O_WRONLY | SCE_O_CREAT | (exclusive ? SCE_O_EXCL : SCE_O_TRUNC);
    SceUID fd = sceIoOpen(path, flags, FILE_PERMS);
    if (fd < 0) return -1;
    char c = value ? '1' : '0';
    int ok = sceIoWrite(fd, &c, 1) == 1;
    if (ok && sceIoSyncByFd(fd, 0) < 0) ok = 0;
    if (sceIoClose(fd) < 0) ok = 0;
    return ok ? 0 : -1;
}

/* Create a new backup, never overwrite a previous user-configured mode. */
static int ensure_backup(const char *src, const char *dst, int dir) {
    if (exists(dst)) return is_expected(dst, dir) ? 0 : -1;
    char tmp[PATH_CAP];
    if (suffix(tmp, sizeof(tmp), dst, ".vsw-init")) return -1;
    /* A previous interrupted initialization may have left only this scratch. */
    if (remove_path(tmp, 0) < 0) return -1;
    if (copy_path(src, tmp, 0) < 0) return -1;
    if (sceIoRename(tmp, dst) < 0) return -1;
    return 0;
}

/* Clear active and parked scratch for one resource. */
static int cleanup_pair(const SwitchPair *p, int old_state) {
    const char *names[] = {p->active, outgoing(p, old_state)};
    const char *suffixes[] = {".vsw-next", ".vsw-old", ".vsw-discard"};
    for (unsigned i = 0; i < 2; i++) {
        for (unsigned j = 0; j < 3; j++) {
            char tmp[PATH_CAP];
            if (suffix(tmp, sizeof(tmp), names[i], suffixes[j]) || remove_path(tmp, 0))
                return -1;
        }
    }
    return 0;
}

/* Originals are preserved at .vsw-old throughout an uncommitted operation. */
static int restore_pair(const SwitchPair *p, int old_state) {
    const char *names[] = {p->active, outgoing(p, old_state)};
    for (unsigned i = 0; i < 2; i++) {
        char previous[PATH_CAP], discard[PATH_CAP];
        if (suffix(previous, sizeof(previous), names[i], ".vsw-old") ||
            suffix(discard, sizeof(discard), names[i], ".vsw-discard")) return -1;
        if (!exists(previous)) continue;
        if (exists(names[i])) {
            if (exists(discard) || sceIoRename(names[i], discard) < 0) return -1;
        }
        if (sceIoRename(previous, names[i]) < 0) return -1;
    }
    return 0;
}
static int finish_cleanup(int old_state) {
    for (unsigned i = 0; i < PAIR_COUNT; i++)
        if (cleanup_pair(&pairs[i], old_state)) return -1;
    if (exists(FILE_COMMIT_TMP) && remove_path(FILE_COMMIT_TMP, 0)) return -1;
    if (exists(FILE_JOURNAL) && sceIoRemove(FILE_JOURNAL) < 0) return -1;
    if (exists(FILE_COMMIT) && sceIoRemove(FILE_COMMIT) < 0) return -1;
    return 0;
}
static int rollback(int old_state) {
    for (unsigned i = 0; i < PAIR_COUNT; i++)
        if (restore_pair(&pairs[i], old_state)) return -1;
    if (write_digit(FILE_STATE, old_state, 0)) return -1;
    return finish_cleanup(old_state);
}

/* Returns 1 if an old transaction was handled; -1 if recovery failed. */
static int recover_if_needed(void) {
    if (!exists(FILE_JOURNAL)) {
        if (exists(FILE_COMMIT)) {
            /* Crash between removing the journal and removing commit marker:
             * scratch cleanup has already succeeded, so just clear the marker.
             */
            int recorded, current;
            if (read_digit(FILE_COMMIT, &recorded) ||
                read_digit(FILE_STATE, &current) || recorded != current) return -1;
            for (unsigned i = 0; i < PAIR_COUNT; i++) {
                const char *names[] = {pairs[i].active, outgoing(&pairs[i], !current)};
                const char *tags[] = {".vsw-next", ".vsw-old", ".vsw-discard"};
                for (unsigned j = 0; j < 2; j++) for (unsigned k = 0; k < 3; k++) {
                    char tmp[PATH_CAP];
                    if (suffix(tmp, sizeof(tmp), names[j], tags[k]) || exists(tmp)) return -1;
                }
            }
            if (sceIoRemove(FILE_COMMIT) < 0) return -1;
            return 1;
        }
        if (exists(FILE_COMMIT_TMP)) return -1;
        return 0;
    }
    int old_state, committed_state;
    if (read_digit(FILE_JOURNAL, &old_state)) return -1;
    if (exists(FILE_COMMIT)) {
        if (read_digit(FILE_COMMIT, &committed_state) || committed_state == old_state)
            return -1;
        if (finish_cleanup(old_state)) return -1;
        return 1;
    }
    if (rollback(old_state)) return -1;
    return 1; /* don't unexpectedly toggle again during recovery */
}

static int initialize(void) {
    int initial_state = 0;
    int state_present = exists(FILE_STATE);
    if (state_present && read_digit(FILE_STATE, &initial_state)) return -1;
    if (!is_expected(FILE_CONFIG, 0)) return -1;
    for (unsigned i = 0; i < PAIR_COUNT; i++) {
        const SwitchPair *p = &pairs[i];
        if (!exists(p->active)) {
            if (p->required || exists(p->portable) || exists(p->docked)) return -1;
            continue;
        }
        if (!is_expected(p->active, p->is_dir)) return -1;
        if (ensure_backup(p->active, p->portable, p->is_dir) ||
            ensure_backup(p->active, p->docked, p->is_dir)) return -1;
    }
    /* Losing the setup marker must never silently reset an existing mode. */
    if (!state_present && write_digit(FILE_STATE, initial_state, 1)) return -1;
    SceUID fd = sceIoOpen(FILE_SWITCHCONF, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, FILE_PERMS);
    if (fd < 0) return -1;
    static const char marker[] = "VitaSwitch 1.22 initialized\n";
    int ok = write_all(fd, marker, (SceSize)(sizeof(marker) - 1)) == 0;
    if (ok && sceIoSyncByFd(fd, 0) < 0) ok = 0;
    if (sceIoClose(fd) < 0) ok = 0;
    if (!ok) { (void)sceIoRemove(FILE_SWITCHCONF); return -1; }
    return 0;
}

/* False for unused optional plugins; error on a missing live profile. */
static int check_pair(const SwitchPair *p, int old_state) {
    const char *from = incoming(p, old_state);
    const char *to = outgoing(p, old_state);
    int live = exists(p->active);
    int has_from = exists(from), has_to = exists(to);
    if (!live) return p->required || has_from || has_to ? -1 : 0;
    if (!is_expected(p->active, p->is_dir)) return -1;
    if ((has_from && !is_expected(from, p->is_dir)) ||
        (has_to && !is_expected(to, p->is_dir))) return -1;
    if (p->required && !has_from) return -1;
    /* If the outgoing backup already exists, a missing incoming backup
     * suggests damage rather than a newly installed plugin: fail closed.
     */
    if (!p->required && !has_from && has_to) return -1;
    return 1;
}

static int prepare_pair(const SwitchPair *p, int old_state) {
    const char *to = outgoing(p, old_state);
    const char *from = incoming(p, old_state);
    /* A newly installed optional plugin starts with the same profiles in both modes. */
    const char *future_active = exists(from) ? from : p->active;
    char active_next[PATH_CAP], outgoing_next[PATH_CAP];
    if (suffix(active_next, sizeof(active_next), p->active, ".vsw-next") ||
        suffix(outgoing_next, sizeof(outgoing_next), to, ".vsw-next")) return -1;
    if (copy_path(future_active, active_next, 0)) return -1;
    if (copy_path(p->active, outgoing_next, 0)) return -1;
    return 0;
}
static int apply_pair(const SwitchPair *p, int old_state) {
    const char *target = outgoing(p, old_state);
    const char *names[] = {p->active, target};
    for (unsigned i = 0; i < 2; i++) {
        char next[PATH_CAP], previous[PATH_CAP];
        if (suffix(next, sizeof(next), names[i], ".vsw-next") ||
            suffix(previous, sizeof(previous), names[i], ".vsw-old")) return -1;
        if (exists(names[i]) && sceIoRename(names[i], previous) < 0) return -1;
        if (sceIoRename(next, names[i]) < 0) return -1;
    }
    return 0;
}

/* The commit marker itself is installed with a rename (no existing target). */
static int write_commit(int new_state) {
    if (write_digit(FILE_COMMIT_TMP, new_state, 1)) return -1;
    if (sceIoRename(FILE_COMMIT_TMP, FILE_COMMIT) < 0) return -1;
    return 0;
}

/* Artwork is optional; preserve its previous version and repair it on launch. */
static int recover_art(const char *dest) {
    char stage[PATH_CAP], backup[PATH_CAP];
    if (suffix(stage, sizeof(stage), dest, ".vsw-art") ||
        suffix(backup, sizeof(backup), dest, ".vsw-art-old")) return -1;
    int had_work = exists(stage) || exists(backup);
    if (exists(backup)) {
        if (exists(dest)) {
            if (remove_path(backup, 0)) return -1;
        } else if (sceIoRename(backup, dest) < 0) return -1;
    }
    if (remove_path(stage, 0)) return -1;
    return had_work ? 1 : 0;
}
static int update_art(const char *src, const char *dest) {
    if (!is_expected(src, 0) || !is_expected(dest, 0)) return -1;
    if (recover_art(dest)) return -1;
    char stage[PATH_CAP], backup[PATH_CAP];
    if (suffix(stage, sizeof(stage), dest, ".vsw-art") ||
        suffix(backup, sizeof(backup), dest, ".vsw-art-old")) return -1;
    if (copy_file(src, stage)) return -1;
    if (sceIoRename(dest, backup) < 0) { (void)remove_path(stage, 0); return -1; }
    if (sceIoRename(stage, dest) < 0) {
        (void)sceIoRename(backup, dest);
        (void)remove_path(stage, 0);
        return -1;
    }
    return remove_path(backup, 0);
}

int main(void) {
    int recovered = recover_if_needed();
    if (recovered < 0) { report_error("Interrupted switch needs recovery; check .vsw-old backups and permissions."); return 1; }
    if (recovered > 0) { report_error("Interrupted switch recovered or cleaned; run VitaSwitch again to switch."); return 0; }
    int art_bg = recover_art(BG_LIVEAREA);
    int art_icon = recover_art(ICON_LIVEAREA);
    if (art_bg < 0 || art_icon < 0) {
        report_error("LiveArea artwork recovery failed; review .vsw-art-old files.");
        return 1;
    }
    if (art_bg > 0 || art_icon > 0) {
        report_error("Interrupted LiveArea artwork refresh recovered; run again to switch.");
        return 0;
    }
    if (!exists(FILE_SWITCHCONF)) {
        if (initialize()) { report_error("First-run setup failed; configurations were not switched."); return 1; }
        (void)sceIoRemove(FILE_ERROR);
        sceKernelDelayThread(200000);
        return 0;
    }
    int old_state;
    if (read_digit(FILE_STATE, &old_state)) {
        /* v1.21 did not create a state file until the first mode switch. */
        if (exists(FILE_STATE) || !exists(FILE_CONFIG_PORT) || !exists(FILE_CONFIG_DOCK) ||
            write_digit(FILE_STATE, 0, 1)) {
            report_error("Invalid or missing mode state; refusing to guess the active configuration.");
            return 1;
        }
        old_state = 0;
    }
    int enabled[PAIR_COUNT];
    for (unsigned i = 0; i < PAIR_COUNT; i++) {
        enabled[i] = check_pair(&pairs[i], old_state);
        if (enabled[i] < 0) { report_error("Active or parked configuration is missing or has an unexpected type."); return 1; }
        const char *names[] = {pairs[i].active, pairs[i].portable, pairs[i].docked};
        const char *tags[] = {".vsw-next", ".vsw-old", ".vsw-discard"};
        for (unsigned j = 0; j < 3; j++) for (unsigned k = 0; k < 3; k++) {
            char tmp[PATH_CAP];
            if (suffix(tmp, sizeof(tmp), names[j], tags[k]) || exists(tmp)) {
                report_error("Unexpected VitaSwitch scratch file; inspect before retrying.");
                return 1;
            }
        }
    }
    if (write_digit(FILE_JOURNAL, old_state, 1)) {
        report_error("Could not create a durable transaction journal; nothing was changed.");
        return 1;
    }
    int ok = 1;
    for (unsigned i = 0; i < PAIR_COUNT; i++)
        if (enabled[i] && prepare_pair(&pairs[i], old_state)) { ok = 0; break; }
    if (ok) for (unsigned i = 0; i < PAIR_COUNT; i++)
        if (enabled[i] && apply_pair(&pairs[i], old_state)) { ok = 0; break; }
    if (ok && write_digit(FILE_STATE, !old_state, 0)) ok = 0;
    if (ok && write_commit(!old_state)) ok = 0;
    if (!ok) {
        if (rollback(old_state)) report_error("Switch failed and rollback is incomplete; preserve .vsw-old files.");
        else report_error("Switch failed; previous configurations restored. No reboot.");
        return 1;
    }
    /* The mode is committed. Cleanup is resumable after an interrupted reboot. */
    if (finish_cleanup(old_state)) report_error("Switch committed; scratch cleanup deferred until next launch.");
    else (void)sceIoRemove(FILE_ERROR);
    if (update_art(old_state ? BG_PORTABLE : BG_DOCKED, BG_LIVEAREA) ||
        update_art(old_state ? ICON_PORTABLE : ICON_DOCKED, ICON_LIVEAREA))
        report_error("Mode committed, but LiveArea artwork could not be refreshed.");
    sceKernelDelayThread(200000);
    if (scePowerRequestColdReset() < 0) {
        report_error("Switch committed but automatic reboot failed; reboot manually.");
        return 1;
    }
    return 0;
}
