#ifndef MOCK_VITA_H
#define MOCK_VITA_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
typedef int SceUID;
typedef uint32_t SceSize;
typedef int32_t SceSSize;
typedef int32_t SceMode;
typedef struct { SceMode st_mode; long long st_size; } SceIoStat;
typedef struct { SceIoStat d_stat; char d_name[256]; } SceIoDirent;
#define SCE_S_IFREG 0020000
#define SCE_S_IFDIR 0010000
#define SCE_S_IFMT  0170000
#define SCE_S_ISREG(m) (((m)&SCE_S_IFMT)==SCE_S_IFREG)
#define SCE_S_ISDIR(m) (((m)&SCE_S_IFMT)==SCE_S_IFDIR)
#define SCE_O_RDONLY 1
#define SCE_O_WRONLY 2
#define SCE_O_CREAT  0x0200
#define SCE_O_TRUNC  0x0400
#define SCE_O_EXCL   0x0800
SceUID sceIoOpen(const char *, int, SceMode);
int sceIoClose(SceUID);
SceSSize sceIoRead(SceUID, void *, SceSize);
SceSSize sceIoWrite(SceUID, const void *, SceSize);
int sceIoSyncByFd(SceUID, int);
int sceIoRemove(const char *);
int sceIoRename(const char *, const char *);
int sceIoMkdir(const char *, SceMode);
int sceIoRmdir(const char *);
int sceIoGetstat(const char *, SceIoStat *);
SceUID sceIoDopen(const char *);
int sceIoDread(SceUID, SceIoDirent *);
int sceIoDclose(SceUID);
int sceKernelDelayThread(unsigned int);
int scePowerRequestColdReset(void);
#endif
