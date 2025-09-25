// main.c
#define SceKernelThreadEventHandler void*
#include <psp2/kernel/threadmgr.h>   // sceKernelDelayThread
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

#define PATH_VITAGFX "ux0:data/VitaGrafix/"
#define VG_CONFIG       PATH_VITAGFX "config.txt"
#define VG_CONFIG_PORT  PATH_VITAGFX "config_portable.txt"
#define VG_CONFIG_DOCK  PATH_VITAGFX "config_docked.txt"

#define PATH_PSVSHELL "ur0:data/PSVshell/"
#define PSV_PROFILES       PATH_PSVSHELL "profiles"
#define PSV_PROFILES_PORT  PATH_PSVSHELL "profiles_portable"
#define PSV_PROFILES_DOCK  PATH_PSVSHELL "profiles_docked"

// LiveArea background files
#define PATH_LIVEAREA_CONTENTS "ur0:appmeta/SWCH00001/livearea/contents/"
#define BG_LIVEAREA        PATH_LIVEAREA_CONTENTS "bg.png"
#define BG_PORTABLE        PATH_LIVEAREA_CONTENTS "bg_portable.png"
#define BG_DOCKED          PATH_LIVEAREA_CONTENTS "bg_docked.png"

// Icon files
#define ICON_LIVEAREA      "ur0:appmeta/SWCH00001/icon0.png"
#define ICON_PORTABLE      PATH_LIVEAREA_CONTENTS "icon_portable.png"
#define ICON_DOCKED        PATH_LIVEAREA_CONTENTS "icon_docked.png"

// --- File helpers ---
static int file_exists(const char *path) {
    SceIoStat stat;
    return sceIoGetstat(path, &stat) >= 0;
}

static void copy_file(const char *src, const char *dst) {
    if (!file_exists(src)) return;
    if (file_exists(dst)) sceIoRemove(dst);

    int fd_src = sceIoOpen(src, SCE_O_RDONLY, 0);
    if (fd_src < 0) return;

    int fd_dst = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 6);
    if (fd_dst < 0) { sceIoClose(fd_src); return; }

    char buf[1024]; SceSize r;
    while ((r = sceIoRead(fd_src, buf, sizeof(buf))) > 0) {
        sceIoWrite(fd_dst, buf, r);
    }

    sceIoClose(fd_src);
    sceIoClose(fd_dst);
}

static void copy_dir_recursive(const char *src_dir, const char *dst_dir) {
    if (!file_exists(dst_dir)) sceIoMkdir(dst_dir, 6);

    SceUID d = sceIoDopen(src_dir);
    if (d < 0) return;

    SceIoDirent dirent;
    while (sceIoDread(d, &dirent) > 0) {
        if (strcmp(dirent.d_name, ".") == 0 || strcmp(dirent.d_name, "..") == 0) continue;

        char src_path[1024], dst_path[1024];
        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, dirent.d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, dirent.d_name);

        SceIoStat st;
        if (sceIoGetstat(src_path, &st) < 0) continue;

        if (SCE_S_ISDIR(st.st_mode)) copy_dir_recursive(src_path, dst_path);
        else copy_file(src_path, dst_path);
    }

    sceIoDclose(d);
}

static void remove_dir_recursive(const char *dirpath) {
    if (!file_exists(dirpath)) return;

    SceUID d = sceIoDopen(dirpath);
    if (d < 0) { sceIoRemove(dirpath); return; }

    SceIoDirent dirent;
    while (sceIoDread(d, &dirent) > 0) {
        if (strcmp(dirent.d_name, ".") == 0 || strcmp(dirent.d_name, "..") == 0) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dirpath, dirent.d_name);

        SceIoStat st;
        if (sceIoGetstat(path, &st) < 0) continue;
        if (SCE_S_ISDIR(st.st_mode)) remove_dir_recursive(path);
        else sceIoRemove(path);
    }
    sceIoDclose(d);
    sceIoRmdir(dirpath);
}

static void safe_rename(const char *src, const char *dst) {
    if (!file_exists(src)) return;

    SceIoStat st;
    if (sceIoGetstat(dst, &st) >= 0) {
        if (SCE_S_ISDIR(st.st_mode)) remove_dir_recursive(dst);
        else sceIoRemove(dst);
    }
    sceIoRename(src, dst);
}

static int read_state() {
    int fd = sceIoOpen(FILE_STATE, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;
    char b = 0;
    sceIoRead(fd, &b, 1);
    sceIoClose(fd);
    return b == '1' ? 1 : 0;
}

static void write_state(int s) {
    int fd = sceIoOpen(FILE_STATE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 6);
    if (fd >= 0) {
        char b = s ? '1' : '0';
        sceIoWrite(fd, &b, 1);
        sceIoClose(fd);
    }
}

static void parse_switchconf(int *has_psvs, int *has_vg) {
    *has_psvs = 0; *has_vg = 0;
    if (!file_exists(FILE_SWITCHCONF)) return;

    int fd = sceIoOpen(FILE_SWITCHCONF, SCE_O_RDONLY, 0);
    if (fd < 0) return;

    char buf[512]; int r = sceIoRead(fd, buf, sizeof(buf)-1);
    sceIoClose(fd);
    if (r <= 0) return;

    buf[r] = '\0';
    if (strstr(buf, "PSVs 1")) *has_psvs = 1;
    if (strstr(buf, "VG 1"))   *has_vg   = 1;
}
// --- Update LiveArea background ---
static void update_livearea_bg(int new_state) {
    const char *src = (new_state == 0) ? BG_PORTABLE : BG_DOCKED;
    copy_file(src, BG_LIVEAREA);  // <-- use copy_file instead of safe_rename
}

// --- Update icon0.png ---
static void update_icon0(int new_state) {
    const char *src = (new_state == 0) ? ICON_PORTABLE : ICON_DOCKED;
    copy_file(src, ICON_LIVEAREA);  // <-- use copy_file instead of safe_rename
}

int main() {
    // Initialization mode
    if (!file_exists(FILE_SWITCHCONF)) {
        if (file_exists(FILE_CONFIG)) {
            copy_file(FILE_CONFIG, FILE_CONFIG_PORT);
            copy_file(FILE_CONFIG, FILE_CONFIG_DOCK);
        }

        int psvs_present = file_exists(PATH_PSVSHELL);
        int vg_present   = file_exists(PATH_VITAGFX);

        if (psvs_present) {
            copy_dir_recursive(PSV_PROFILES, PSV_PROFILES_PORT);
            copy_dir_recursive(PSV_PROFILES, PSV_PROFILES_DOCK);
        }

        if (vg_present && file_exists(VG_CONFIG)) {
            copy_file(VG_CONFIG, VG_CONFIG_PORT);
            copy_file(VG_CONFIG, VG_CONFIG_DOCK);
        }

        int fd = sceIoOpen(FILE_SWITCHCONF, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 6);
        if (fd >= 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "OK; PSVs %d; VG %d", psvs_present ? 1 : 0, vg_present ? 1 : 0);
            sceIoWrite(fd, buf, strlen(buf));
            sceIoClose(fd);
        }

        sceKernelDelayThread(200 * 1000); // 0.2s
        return 0;
    }

    // Switching mode
    int psvs_flag = 0, vg_flag = 0;
    parse_switchconf(&psvs_flag, &vg_flag);

    int state = read_state();
    int new_state = (state == 0) ? 1 : 0;

    // Switch tai config
    if (state == 0) {
        safe_rename(FILE_CONFIG, FILE_CONFIG_PORT);
        safe_rename(FILE_CONFIG_DOCK, FILE_CONFIG);
    } else {
        safe_rename(FILE_CONFIG, FILE_CONFIG_DOCK);
        safe_rename(FILE_CONFIG_PORT, FILE_CONFIG);
    }
    write_state(new_state);

    // Switch PSVshell profiles
    if (psvs_flag) {
        if (state == 0) {
            safe_rename(PSV_PROFILES, PSV_PROFILES_PORT);
            safe_rename(PSV_PROFILES_DOCK, PSV_PROFILES);
        } else {
            safe_rename(PSV_PROFILES, PSV_PROFILES_DOCK);
            safe_rename(PSV_PROFILES_PORT, PSV_PROFILES);
        }
    }

    // Switch VitaGrafix config
    if (vg_flag) {
        if (state == 0) {
            safe_rename(VG_CONFIG, VG_CONFIG_PORT);
            safe_rename(VG_CONFIG_DOCK, VG_CONFIG);
        } else {
            safe_rename(VG_CONFIG, VG_CONFIG_DOCK);
            safe_rename(VG_CONFIG_PORT, VG_CONFIG);
        }
    }

    // Switch LiveArea background
    update_livearea_bg(new_state);

    // Switch icon0.png
    update_icon0(new_state);

    sceKernelDelayThread(200 * 1000);
    scePowerRequestColdReset();
    return 0;
}
