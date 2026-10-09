#define _POSIX_C_SOURCE 200809L
#include "mock_vita.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

static DIR *dirs[512];
static unsigned op_count = 0;
static char scratch[2048];
static const char *hostpath(const char *p) {
    const char *root = getenv("VITA_MOCK_ROOT");
    if (!root) abort();
    const char *colon = strchr(p, ':');
    if (!colon || (colon - p) > 32) abort();
    snprintf(scratch, sizeof(scratch), "%s/%.*s/%s", root, (int)(colon-p), p, colon + 1 + (colon[1]=='/'));
    return scratch;
}
static int inject(const char *op, const char *path) {
    const char *failop = getenv("VITA_INJECT_OP");
    if (!failop || strcmp(failop, op)) return 0;
    const char *filter = getenv("VITA_INJECT_PATH");
    if (filter && (!path || !strstr(path, filter))) return 0;
    op_count++;
    const char *nstr = getenv("VITA_INJECT_AT");
    unsigned n = nstr ? (unsigned)strtoul(nstr, 0, 10) : 1;
    if (op_count != n) return 0;
    if (getenv("VITA_INJECT_CRASH")) _exit(77);
    errno = EIO;
    return 1;
}
static int openflags(int f) {
    int result = (f & SCE_O_WRONLY) ? O_WRONLY : O_RDONLY;
    if (f & SCE_O_CREAT) result |= O_CREAT;
    if (f & SCE_O_TRUNC) result |= O_TRUNC;
    if (f & SCE_O_EXCL) result |= O_EXCL;
    return result;
}
SceUID sceIoOpen(const char *p, int f, SceMode m) {
    if (inject("open", p)) return -1;
    return open(hostpath(p), openflags(f), (mode_t)m);
}
int sceIoClose(SceUID fd) { if (inject("close", NULL)) return -1; return close(fd); }
SceSSize sceIoRead(SceUID fd, void *b, SceSize n) { if (inject("read", NULL)) return -1; return (SceSSize)read(fd,b,n); }
SceSSize sceIoWrite(SceUID fd, const void *b, SceSize n) {
    if (inject("write", NULL)) return -1;
    if (getenv("VITA_SHORT_WRITES") && n > 1) n = 1;
    return (SceSSize)write(fd,b,n);
}
int sceIoSyncByFd(SceUID fd, int flags) { (void)flags; if (inject("sync", NULL)) return -1; return fsync(fd); }
int sceIoRemove(const char *p) { if (inject("remove", p)) return -1; return unlink(hostpath(p)); }
int sceIoRename(const char *src, const char *dest) {
    if (inject("rename", src)) return -1;
    /* Simulate a Vita filesystem that refuses to overwrite existing paths. */
    char a[2048], b[2048];
    strcpy(a, hostpath(src)); strcpy(b, hostpath(dest));
    if (access(b,F_OK) == 0) { errno = EEXIST; return -1; }
    return rename(a,b);
}
int sceIoMkdir(const char *p, SceMode m) { if (inject("mkdir", p)) return -1; return mkdir(hostpath(p), (mode_t)m); }
int sceIoRmdir(const char *p) { if (inject("rmdir", p)) return -1; return rmdir(hostpath(p)); }
int sceIoGetstat(const char *p, SceIoStat *st) {
    if (inject("stat", p)) return -1;
    struct stat s;
    if (lstat(hostpath(p),&s)<0) return -1;
    st->st_size=s.st_size;
    st->st_mode=S_ISDIR(s.st_mode)?SCE_S_IFDIR:S_ISREG(s.st_mode)?SCE_S_IFREG:0;
    return 0;
}
SceUID sceIoDopen(const char *p) {
    if (inject("dopen", p)) return -1;
    DIR *dir = opendir(hostpath(p));
    if (!dir) return -1;
    for (int i=0;i<512;i++) if (!dirs[i]) {dirs[i]=dir;return 1000+i;}
    closedir(dir); return -1;
}
int sceIoDread(SceUID fd,SceIoDirent *ent) {
    if (inject("dread",NULL)) return -1;
    if (fd < 1000 || fd>=1512 || !dirs[fd-1000]) return -1;
    errno=0;
    struct dirent *d = readdir(dirs[fd-1000]);
    if (!d) return errno ? -1 : 0;
    strncpy(ent->d_name,d->d_name,sizeof(ent->d_name)-1);
    ent->d_name[sizeof(ent->d_name)-1]=0;
    return 1;
}
int sceIoDclose(SceUID fd) {
    if (fd<1000 || fd>=1512 || !dirs[fd-1000]) return -1;
    DIR *d=dirs[fd-1000]; dirs[fd-1000]=NULL;
    if (inject("dclose",NULL)) { closedir(d);return -1; }
    return closedir(d);
}
int sceKernelDelayThread(unsigned int n) { (void)n; return 0; }
int scePowerRequestColdReset(void) {
    const char *root = getenv("VITA_MOCK_ROOT");
    if (!root) abort();
    char p[2048]; snprintf(p,sizeof(p),"%s/rebooted",root);
    FILE *f=fopen(p,"w"); if (!f) return -1;
    fputs("1",f); fclose(f);return 0;
}
