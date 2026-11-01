/**
 ******************************************************************************
 * @file    storage.c
 * @brief   存储抽象层实现（FatFs + SD 卡 / 板载 SPI Flash）
 * @note    挂载顺序：先试 SD 卡("0:")，失败再试 W25Q128("1:")；
 *          Flash 若是首次使用没有文件系统，会自动格式化（约 12MB，需数秒）。
 ******************************************************************************
 */
#include <string.h>
#include <stdio.h>
#include "storage.h"
#include "ff.h"
#include "sdio_sdcard.h"

/* 每个逻辑驱动器必须各自一个 FATFS 对象（FatFs 按卷号保存对象指针） */
static FATFS      s_fs0;                 /* 0: SD 卡 */
static FATFS      s_fs1;                 /* 1: 板载 SPI Flash */
static u8         s_has_sd = 0;
static u8         s_has_flash = 0;
static storage_dev_t s_dev = STORAGE_NONE;
static FIL        s_rec;
static u8         s_rec_open = 0;
static char       s_vol[3] = "0:";

/* 目录遍历用的对象放到静态区：_USE_LFN=1 时 DIR/FILINFO 各带一个
 * (_MAX_LFN+1) 的长文件名缓冲，合计约 1KB，放栈上会撑爆 1KB 默认栈。 */
static DIR        s_dir;
static FILINFO    s_fno;

/* SD 卡与摄像头共用 PC8/9/11，访问 SD 前先把数据线抢回 SDIO */
static void storage_pins(void)
{
    if (s_dev == STORAGE_SD)
    {
        SDIO_PinsConfig();
    }
}

/* 复制文件用的句柄与缓冲区（放静态区，避免占栈） */
static FIL s_cpsrc;
static FIL s_cpdst;
static u8  s_cpbuf[512];

/* 目录里的"伪项"过滤：卷标(AM_VOL)、空名、全空格名都不该出现在文件列表里 */
static u8 storage_fno_skip(const FILINFO *fno)
{
    u32 i;
    u8  blank = 1;

    if (fno->fattrib & AM_VOL)
    {
        return 1;                        /* 卷标，不是文件也不是目录 */
    }
    if (fno->fname[0] == 0)
    {
        return 1;
    }
    for (i = 0; (i < 12) && fno->fname[i]; i++)
    {
        if (fno->fname[i] != ' ')
        {
            blank = 0;
            break;
        }
    }
    return blank;
}

/* 挂载某个逻辑驱动器 */
static u8 storage_mount(FATFS *fs, const char *vol)
{
    if (f_mount(fs, (TCHAR *)vol, 1) == FR_OK)
    {
        return 0;
    }
    return 1;
}

u8 Storage_Init(void)
{
    s_dev = STORAGE_NONE;

    /* 1) 探测 SD 卡(0:) 与 板载 Flash(1:)，两者可同时挂载 */
    s_has_sd    = (storage_mount(&s_fs0, "0:") == 0) ? 1 : 0;
    s_has_flash = (storage_mount(&s_fs1, "1:") == 0) ? 1 : 0;

    /* 2) Flash 首次使用没有文件系统：仅在无 SD 时自动格式化(约 12MB，需数秒) */
    if (!s_has_flash && !s_has_sd)
    {
        if (f_mkfs((TCHAR *)"1:", 0, 0) == FR_OK)
        {
            s_has_flash = (storage_mount(&s_fs1, "1:") == 0) ? 1 : 0;
        }
    }

    /* 3) 默认卷：优先 SD 卡，其次 Flash */
    if (s_has_sd)
    {
        s_dev = STORAGE_SD;
        strcpy(s_vol, "0:");
        return 0;
    }
    if (s_has_flash)
    {
        s_dev = STORAGE_FLASH;
        strcpy(s_vol, "1:");
        return 0;
    }

    return 1;
}

u8 Storage_HasSd(void)
{
    return s_has_sd;
}

u8 Storage_HasFlash(void)
{
    return s_has_flash;
}

u8 Storage_SetDev(storage_dev_t dev)
{
    if ((dev == STORAGE_SD) && s_has_sd)
    {
        s_dev = STORAGE_SD;
        strcpy(s_vol, "0:");
        return 0;
    }
    if ((dev == STORAGE_FLASH) && s_has_flash)
    {
        s_dev = STORAGE_FLASH;
        strcpy(s_vol, "1:");
        return 0;
    }
    return 1;
}

storage_dev_t Storage_Dev(void)
{
    return s_dev;
}

const char *Storage_DevName(void)
{
    switch (s_dev)
    {
        case STORAGE_SD:    return "SD";
        case STORAGE_FLASH: return "FLASH";
        default:            return "NONE";
    }
}

const char *Storage_Prefix(void)
{
    return (s_dev == STORAGE_NONE) ? "" : s_vol;
}

void Storage_GetSpace(u32 *free_kb, u32 *total_kb)
{
    DWORD  fre_clust = 0;
    FATFS *fsp = 0;

    *free_kb  = 0;
    *total_kb = 0;

    if (s_dev == STORAGE_NONE)
    {
        return;
    }

    storage_pins();

    if (f_getfree((TCHAR *)s_vol, &fre_clust, &fsp) == FR_OK)
    {
        u32 tot_sect = (fsp->n_fatent - 2) * fsp->csize;
        u32 fre_sect = fre_clust * fsp->csize;

        *total_kb = tot_sect / 2;       /* 每扇区 512 字节 */
        *free_kb  = fre_sect / 2;
    }
}

u8 Storage_SaveFile(const char *name, const u8 *data, u32 len)
{
    FIL     fil;
    UINT    bw = 0;
    FRESULT r;
    char    path[32];

    if (s_dev == STORAGE_NONE)
    {
        return 1;
    }

    storage_pins();

    snprintf(path, sizeof(path), "%s%s", s_vol, name);

    r = f_open(&fil, (TCHAR *)path, FA_CREATE_ALWAYS | FA_WRITE);
    if (r != FR_OK)
    {
        return 2;
    }

    r = f_write(&fil, data, len, &bw);
    f_close(&fil);

    return (r == FR_OK && bw == len) ? 0 : 3;
}

u8 Storage_RecOpen(const char *name)
{
    FRESULT r;
    char    path[32];

    if (s_dev == STORAGE_NONE || s_rec_open)
    {
        return 1;
    }

    storage_pins();

    snprintf(path, sizeof(path), "%s%s", s_vol, name);

    r = f_open(&s_rec, (TCHAR *)path, FA_CREATE_ALWAYS | FA_WRITE);
    if (r != FR_OK)
    {
        return 2;
    }

    s_rec_open = 1;
    return 0;
}

u8 Storage_RecWrite(const u8 *data, u32 len)
{
    UINT    bw = 0;
    FRESULT r;

    if (!s_rec_open)
    {
        return 1;
    }

    storage_pins();

    r = f_write(&s_rec, data, len, &bw);

    return (r == FR_OK && bw == len) ? 0 : 2;
}

void Storage_RecClose(void)
{
    if (s_rec_open)
    {
        storage_pins();
        f_close(&s_rec);
        s_rec_open = 0;
    }
}

u8 Storage_IsRecOpen(void)
{
    return s_rec_open;
}

u32 Storage_FileCount(void)
{
    u32 n = 0;

    if (s_dev == STORAGE_NONE)
    {
        return 0;
    }

    storage_pins();

    if (f_opendir(&s_dir, (TCHAR *)s_vol) == FR_OK)
    {
        while (f_readdir(&s_dir, &s_fno) == FR_OK && s_fno.fname[0] != 0)
        {
            if (storage_fno_skip(&s_fno))
            {
                continue;
            }
            if (!(s_fno.fattrib & AM_DIR))
            {
                n++;
            }
        }
        f_closedir(&s_dir);
    }

    return n;
}

u32 Storage_ListFiles(storage_file_t *out, u32 max)
{
    u32 n = 0;

    if ((s_dev == STORAGE_NONE) || (out == 0) || (max == 0))
    {
        return 0;
    }

    storage_pins();

    if (f_opendir(&s_dir, (TCHAR *)s_vol) == FR_OK)
    {
        while ((f_readdir(&s_dir, &s_fno) == FR_OK) && (s_fno.fname[0] != 0))
        {
            if (s_fno.fattrib & AM_DIR)
            {
                continue;
            }
            if (n < max)
            {
                u32 i = 0;

                while ((s_fno.fname[i] != 0) && (i < (STORAGE_NAME_LEN - 1)))
                {
                    out[n].name[i] = s_fno.fname[i];
                    i++;
                }
                out[n].name[i]  = 0;
                out[n].size_kb  = (u32)((s_fno.fsize + 1023U) / 1024U);
                n++;
            }
        }
        f_closedir(&s_dir);
    }

    return n;
}

u8 Storage_DeleteFile(const char *name)
{
    char    path[40];

    if ((s_dev == STORAGE_NONE) || (name == 0) || (name[0] == 0))
    {
        return 1;
    }

    storage_pins();

    snprintf(path, sizeof(path), "%s%s", s_vol, name);

    return (f_unlink((TCHAR *)path) == FR_OK) ? 0 : 2;
}

/* ==================== 目录感知的文件管理接口 ==================== */

void Storage_AbsPath(const char *rel, char *out, u32 n)
{
    const char *pfx = Storage_Prefix();
    u32 i = 0;

    if ((out == 0) || (n == 0))
    {
        return;
    }

    while (*pfx && ((i + 1) < n))
    {
        out[i++] = *pfx++;
    }
    if (rel != 0)
    {
        while (*rel && ((i + 1) < n))
        {
            out[i++] = *rel++;
        }
    }
    out[i] = 0;
}

u32 Storage_ListDir(const char *rel, storage_entry_t *out, u32 max)
{
    char  path[96];
    u32   n = 0, nd = 0;

    if ((s_dev == STORAGE_NONE) || (out == 0) || (max == 0))
    {
        return 0;
    }

    storage_pins();
    Storage_AbsPath(rel, path, sizeof(path));

    if (f_opendir(&s_dir, (TCHAR *)path) != FR_OK)
    {
        return 0;
    }

    while ((f_readdir(&s_dir, &s_fno) == FR_OK) && (s_fno.fname[0] != 0))
    {
        storage_entry_t e;
        u32 i = 0;

        if ((strcmp(s_fno.fname, ".") == 0) || (strcmp(s_fno.fname, "..") == 0))
        {
            continue;
        }
        if (storage_fno_skip(&s_fno))
        {
            continue;                    /* 卷标 / 空名，不显示 */
        }
        if (n >= max)
        {
            break;
        }

        while (s_fno.fname[i] && (i < (STORAGE_ENTRY_NAME - 1)))
        {
            e.name[i] = s_fno.fname[i];
            i++;
        }
        e.name[i]  = 0;
        e.size_kb  = (u32)((s_fno.fsize + 1023U) / 1024U);
        e.is_dir   = (s_fno.fattrib & AM_DIR) ? 1 : 0;

        if (e.is_dir)
        {
            /* 目录插到前面，保持"目录在前、文件在后" */
            memmove(&out[nd + 1], &out[nd], (n - nd) * sizeof(storage_entry_t));
            out[nd] = e;
            nd++;
            n++;
        }
        else
        {
            out[n++] = e;
        }
    }

    f_closedir(&s_dir);
    return n;
}

u8 Storage_CopyFile(const char *src_rel, const char *dst_rel)
{
    char sp[96], dp[96];
    UINT br = 0, bw = 0;
    u8   ok = 1;

    if (s_dev == STORAGE_NONE)
    {
        return 1;
    }

    storage_pins();
    Storage_AbsPath(src_rel, sp, sizeof(sp));
    Storage_AbsPath(dst_rel, dp, sizeof(dp));

    /* 目标已存在则不覆盖，避免误删数据 */
    if (f_stat((TCHAR *)dp, &s_fno) == FR_OK)
    {
        return 6;
    }

    if (f_open(&s_cpsrc, (TCHAR *)sp, FA_READ) != FR_OK)
    {
        return 2;
    }
    if (f_open(&s_cpdst, (TCHAR *)dp, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        f_close(&s_cpsrc);
        return 3;
    }

    for (;;)
    {
        if (f_read(&s_cpsrc, s_cpbuf, sizeof(s_cpbuf), &br) != FR_OK)
        {
            ok = 0;
            break;
        }
        if (br == 0)
        {
            break;
        }
        if ((f_write(&s_cpdst, s_cpbuf, br, &bw) != FR_OK) || (bw != br))
        {
            ok = 0;
            break;
        }
    }

    f_close(&s_cpsrc);
    f_close(&s_cpdst);

    if (!ok)
    {
        storage_pins();
        f_unlink((TCHAR *)dp);          /* 删掉半成品 */
        return 4;
    }
    return 0;
}

u8 Storage_MoveFile(const char *src_rel, const char *dst_rel)
{
    char sp[96], dp[96];

    if (s_dev == STORAGE_NONE)
    {
        return 1;
    }

    storage_pins();
    Storage_AbsPath(src_rel, sp, sizeof(sp));
    Storage_AbsPath(dst_rel, dp, sizeof(dp));

    /* 目标已存在则不覆盖 */
    if (f_stat((TCHAR *)dp, &s_fno) == FR_OK)
    {
        return 6;
    }

    if (f_rename((TCHAR *)sp, (TCHAR *)dp) == FR_OK)
    {
        return 0;                       /* 同卷直接改名 */
    }

    /* 跨卷/改名失败：复制后删除源文件 */
    if (Storage_CopyFile(src_rel, dst_rel) == 0)
    {
        storage_pins();
        if (f_unlink((TCHAR *)sp) == FR_OK)
        {
            return 0;
        }
    }
    return 5;
}

u8 Storage_DeletePath(const char *rel)
{
    char p[96];

    if (s_dev == STORAGE_NONE)
    {
        return 1;
    }

    storage_pins();
    Storage_AbsPath(rel, p, sizeof(p));

    return (f_unlink((TCHAR *)p) == FR_OK) ? 0 : 2;
}
