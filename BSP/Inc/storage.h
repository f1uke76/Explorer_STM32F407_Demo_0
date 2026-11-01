/**
 ******************************************************************************
 * @file    storage.h
 * @brief   存储抽象层：优先 SD 卡，无卡时自动回落到板载 SPI Flash(W25Q128)
 *          两者都通过 FatFs 访问，上层读写文件的代码完全一致
 ******************************************************************************
 */
#ifndef __STORAGE_H
#define __STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef enum
{
    STORAGE_NONE = 0,
    STORAGE_SD,
    STORAGE_FLASH
} storage_dev_t;

#define STORAGE_MAX_FILES   32
#define STORAGE_NAME_LEN    16

/* 目录里的一条文件记录 */
typedef struct
{
    char name[STORAGE_NAME_LEN];   /* 文件名(含扩展名) */
    u32  size_kb;                  /* 文件大小(KB，向上取整) */
} storage_file_t;

u8          Storage_Init(void);                 /* 0: 成功(已挂载某个设备) */
storage_dev_t Storage_Dev(void);
const char *Storage_DevName(void);              /* "SD" / "FLASH" / "NONE" */
const char *Storage_Prefix(void);               /* 卷前缀 "0:" / "1:" / "" */

/* 设备探测与切换：SD 卡(0:) 与 板载 Flash(1:) 可同时挂载，随时切换当前卷 */
u8   Storage_HasSd(void);
u8   Storage_HasFlash(void);
u8   Storage_SetDev(storage_dev_t dev);         /* 0: 切换成功 */
void        Storage_GetSpace(u32 *free_kb, u32 *total_kb);

u8   Storage_SaveFile(const char *name, const u8 *data, u32 len);  /* 覆盖写 */
u8   Storage_RecOpen(const char *name);         /* 录像: 打开文件准备追加 */
u8   Storage_RecWrite(const u8 *data, u32 len);
void Storage_RecClose(void);
u8   Storage_IsRecOpen(void);

u32  Storage_FileCount(void);                   /* 根目录文件数(粗略) */

/* 枚举根目录文件(最多 max 条)，返回实际文件数 */
u32  Storage_ListFiles(storage_file_t *out, u32 max);
/* 删除根目录下的文件，0:成功 */
u8   Storage_DeleteFile(const char *name);

/* ---------------- 目录感知的文件管理接口 ---------------- */

#define STORAGE_MAX_ENTRIES  40
#define STORAGE_ENTRY_NAME   40

typedef struct
{
    char name[STORAGE_ENTRY_NAME];
    u32  size_kb;
    u8   is_dir;
} storage_entry_t;

/* 相对路径 -> 带卷前缀的绝对路径：rel="" -> "0:"，rel="/A" -> "0:/A" */
void Storage_AbsPath(const char *rel, char *out, u32 n);

/* 列出目录 rel（""=根）；目录排在前面，文件在后 */
u32  Storage_ListDir(const char *rel, storage_entry_t *out, u32 max);

/* 复制 / 移动文件（rel 为相对卷根的完整路径），0:成功 */
u8   Storage_CopyFile(const char *src_rel, const char *dst_rel);
u8   Storage_MoveFile(const char *src_rel, const char *dst_rel);

/* 删除文件（或空目录），0:成功 */
u8   Storage_DeletePath(const char *rel);

#ifdef __cplusplus
}
#endif

#endif /* __STORAGE_H */
