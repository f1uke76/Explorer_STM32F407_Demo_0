/**
 ******************************************************************************
 * @file    diskio.c
 * @brief   FatFs 底层磁盘接口
 *          逻辑驱动器 0 = SD 卡（SDIO）
 *          逻辑驱动器 1 = 板载 SPI Flash W25Q128（前 12MB 作为 FAT 卷）
 ******************************************************************************
 */
#include "diskio.h"
#include "sdio_sdcard.h"
#include "w25qxx.h"

#define SD_CARD           0
#define EX_FLASH          1

#define FLASH_SECTOR_SIZE   512
#define FLASH_BLOCK_SIZE    8
#define FLASH_SECTOR_COUNT  (2048 * 12)     /* W25Q128 前 12MB */

DSTATUS disk_status(BYTE pdrv)
{
    switch (pdrv)
    {
        case SD_CARD:
        case EX_FLASH:
            return RES_OK;
        default:
            return STA_NOINIT;
    }
}

DSTATUS disk_initialize(BYTE pdrv)
{
    u8 res;

    switch (pdrv)
    {
        case SD_CARD:
            res = SD_Init();
            break;

        case EX_FLASH:
            W25QXX_Init();
            res = (W25QXX_TYPE == 0) ? 1 : 0;
            break;

        default:
            res = 1;
            break;
    }

    return res ? STA_NOINIT : 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    u8 res;

    if (pdrv == SD_CARD)
    {
        res = SD_ReadDisk(buff, sector, count);
        if (res)                            /* 读失败时重新初始化再试一次 */
        {
            SD_Init();
            res = SD_ReadDisk(buff, sector, count);
        }
    }
    else if (pdrv == EX_FLASH)
    {
        res = 0;
        while (count--)
        {
            W25QXX_Read(buff, sector * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
            sector++;
            buff += FLASH_SECTOR_SIZE;
        }
    }
    else
    {
        res = 1;
    }

    return res ? RES_ERROR : RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    u8 res;

    if (pdrv == SD_CARD)
    {
        res = SD_WriteDisk((u8 *)buff, sector, count);
        if (res)
        {
            SD_Init();
            res = SD_WriteDisk((u8 *)buff, sector, count);
        }
    }
    else if (pdrv == EX_FLASH)
    {
        res = 0;
        while (count--)
        {
            W25QXX_Write((u8 *)buff, sector * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
            sector++;
            buff += FLASH_SECTOR_SIZE;
        }
    }
    else
    {
        res = 1;
    }

    return res ? RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    DRESULT res = RES_PARERR;

    if (pdrv == SD_CARD)
    {
        switch (cmd)
        {
            case CTRL_SYNC:
                res = RES_OK;
                break;
            case GET_SECTOR_SIZE:
                *(WORD *)buff = 512;
                res = RES_OK;
                break;
            case GET_BLOCK_SIZE:
                *(WORD *)buff = SDCardInfo.LogBlockSize;
                res = RES_OK;
                break;
            case GET_SECTOR_COUNT:
                *(DWORD *)buff = SDCardInfo.LogBlockNbr;
                res = RES_OK;
                break;
            default:
                break;
        }
    }
    else if (pdrv == EX_FLASH)
    {
        switch (cmd)
        {
            case CTRL_SYNC:
                res = RES_OK;
                break;
            case GET_SECTOR_SIZE:
                *(WORD *)buff = FLASH_SECTOR_SIZE;
                res = RES_OK;
                break;
            case GET_BLOCK_SIZE:
                *(WORD *)buff = FLASH_BLOCK_SIZE;
                res = RES_OK;
                break;
            case GET_SECTOR_COUNT:
                *(DWORD *)buff = FLASH_SECTOR_COUNT;
                res = RES_OK;
                break;
            default:
                break;
        }
    }

    return res;
}

/* FatFs 需要的时间戳，这里固定返回 0（本工程未用 RTC） */
DWORD get_fattime(void)
{
    return 0;
}
