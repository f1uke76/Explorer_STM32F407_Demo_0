/**
 ******************************************************************************
 * @file    jpeg_view.c
 * @brief   TJpgDec 解码 JPEG 显示到 LCD；支持 MJPEG 连续帧播放
 * @note    一帧 JPEG 先整帧读进内存（复用摄像头的 64KB 缓冲区），
 *          再由 TJpgDec 从内存流式解码，按 MCU 矩形块直接刷进 LCD 显存。
 ******************************************************************************
 */
#include <string.h>
#include "jpeg_view.h"
#include "tjpgd.h"
#include "ff.h"
#include "lcd.h"
#include "key.h"
#include "camera.h"
#include "sdio_sdcard.h"

/* TJpgDec 工作内存池：输入缓冲 1KB + Huffman 表(最大约3KB) + 量化表1KB
 * + IDCT/色度缓冲约1KB，最坏约 6KB，这里给 8KB 留余量 */
#define JD_WORK_SIZE   8192U

static u8 s_work[JD_WORK_SIZE] __attribute__((aligned(4)));

/* 输出位置（图片左上角在屏幕上的坐标），由解码前设置 */
static u16 s_ox, s_oy;

/* 诊断信息：最近一次读取的字节数与文件头/尾两字节(大端) */
volatile u32 Jpeg_DbgLen = 0;
volatile u16 Jpeg_DbgHdr = 0;
volatile u16 Jpeg_DbgTail = 0;

/* 解码可打断：out_lcd 每画一行就查一次按键，有键按下立即中止。
 * 中止时顺手把按键方向抓下来存进 Jpeg_KeyEvent，避免这个按键被
 * KEY_GetDir 的消抖二次检查吞掉（否则会出现"画面停了但没切换"）。 */
volatile u8 Jpeg_Aborted  = 0;
volatile u8 Jpeg_KeyEvent = 0;      /* 中止时捕获到的按键方向(0=无) */

/* 内存输入流上下文 */
typedef struct
{
    const u8 *data;
    u32       len;
    u32       pos;
} memctx_t;

static memctx_t s_mem;

/* ---------------- TJpgDec 回调 ---------------- */

static UINT in_mem(JDEC *jd, BYTE *buf, UINT nbyte)
{
    memctx_t *m = (memctx_t *)jd->device;
    UINT n = nbyte;

    if (m == 0)
    {
        return 0;
    }
    if ((m->pos + n) > m->len)
    {
        n = (UINT)(m->len - m->pos);
    }

    memcpy(buf, m->data + m->pos, n);
    m->pos += n;

    return n;
}

/* 把一块解码结果（RGB565）刷到 LCD。rect 坐标已是缩放后的输出坐标 */
static UINT out_lcd(JDEC *jd, void *bitmap, JRECT *rect)
{
    WORD *src = (WORD *)bitmap;
    u16   w   = (u16)(rect->right - rect->left + 1);
    u16   h   = (u16)(rect->bottom - rect->top + 1);
    u16   dx  = (u16)(s_ox + rect->left);
    u16   dy  = (u16)(s_oy + rect->top);
    u16   r, c;

    (void)jd;

    for (r = 0; r < h; r++)
    {
        /* 每画一行查一次按键：有键立刻中止，并把方向抓下来给主循环 */
        if (KEY_AnyDown())
        {
            key_dir_t kd = KEY_GetDir();

            if (kd != KEY_DIR_NONE)
            {
                Jpeg_KeyEvent = (u8)kd;
            }
            Jpeg_Aborted = 1;
            return 1;
        }

        LCD_SetCursor(dx, (u16)(dy + r));
        LCD_WriteRAM_Prepare();
        for (c = 0; c < w; c++)
        {
            LCD->LCD_RAM = src[(u32)r * w + c];
        }
    }

    /* 注意：本工程用的 tjpgd 被 ATK 改成 `return (JRESULT)outfunc(...)`，
     * 直接把返回值当 JRESULT 用，所以这里必须返回 0(JDR_OK) 才表示“继续”，
     * 返回 1 会被当成 JDR_INTR，解完第一块就中止。 */
    return 0;
}

/* ---------------- 从文件里抠出一整帧 JPEG ---------------- */

/**
 * @brief 从文件当前位置读取，找到下一个 SOI(FFD8) 并一直读到 EOI(FFD9)
 * @param dst 目标缓冲区  @param cap 容量
 * @return 帧字节数；0 表示文件结束/未找到完整帧
 */
static u32 extract_frame(FIL *fp, u8 *dst, u32 cap)
{
    static u8 chunk[512];
    UINT br = 0;
    u32  n = 0;
    u8   prev = 0;
    u8   in = 0;
    u8   eof = 0;

    while (!eof)
    {
        UINT i;

        if (f_read(fp, chunk, sizeof(chunk), &br) != FR_OK)
        {
            return 0;
        }
        if (br == 0)
        {
            eof = 1;
        }

        for (i = 0; i < br; i++)
        {
            u8 b = chunk[i];

            if (!in)
            {
                if ((prev == 0xFF) && (b == 0xD8))
                {
                    if (cap >= 2)
                    {
                        dst[0] = 0xFF;
                        dst[1] = 0xD8;
                    }
                    n  = 2;
                    in = 1;
                }
            }
            else
            {
                if (n < cap)
                {
                    dst[n++] = b;
                }
                if ((prev == 0xFF) && (b == 0xD9))
                {
                    return n;              /* 完整一帧 */
                }
            }
            prev = b;
        }

        if (eof && in)
        {
            return n;                      /* 文件被截断，返回已有的 */
        }
    }

    return 0;
}

/* ---------------- 解码一帧（内存 -> LCD） ---------------- */

static u8 decode_buffer(const u8 *data, u32 len, u16 max_w, u16 max_h)
{
    JDEC    jd;
    JRESULT res;
    u8      scale = 0;
    u16     iw, ih;
    u16     ox = s_ox, oy = s_oy;

    s_mem.data = data;
    s_mem.len  = len;
    s_mem.pos  = 0;

    res = jd_prepare(&jd, in_mem, s_work, JD_WORK_SIZE, &s_mem);
    if (res != JDR_OK)
    {
        return (u8)(10 + (u8)res);      /* 11..18：区分 prepare 的具体错误 */
    }

    while ((scale < 3) && (((jd.width >> scale) > max_w) || ((jd.height >> scale) > max_h)))
    {
        scale++;
    }

    iw = (u16)(jd.width  >> scale);
    ih = (u16)(jd.height >> scale);

    /* 在 max_w x max_h 区域内居中 */
    s_ox = (u16)((max_w > iw) ? (ox + (max_w - iw) / 2) : ox);
    s_oy = (u16)((max_h > ih) ? (oy + (max_h - ih) / 2) : oy);

    res = jd_decomp(&jd, out_lcd, scale);
    if (res != JDR_OK)
    {
        return (u8)(20 + (u8)res);      /* 21..28：区分 decomp 的具体错误 */
    }

    return 0;
}

/* ---------------- 对外接口 ---------------- */

u8 Jpeg_ShowFile(const char *path, u16 x, u16 y, u16 max_w, u16 max_h)
{
    FIL  fp;
    UINT br = 0;
    u32  n, off = 0;
    u8   r;
    u8  *buf = Camera_BufPtr();

    Jpeg_DbgLen = 0;
    Jpeg_DbgHdr = 0;
    Jpeg_DbgTail = 0;

    if (buf == 0)
    {
        return 4;
    }

    SDIO_PinsConfig();          /* SD 与 DCMI 共用 PC8/9/11，读文件前先抢回 SDIO */

    if (f_open(&fp, (const TCHAR *)path, FA_READ) != FR_OK)
    {
        /* 引脚来回切换后 SDIO 偶尔会失步，重新识别一次卡再试 */
        (void)SD_Init();
        SDIO_PinsConfig();
        if (f_open(&fp, (const TCHAR *)path, FA_READ) != FR_OK)
        {
            return 3;
        }
    }

    /* 整文件读入（照片一般 20KB 左右，远小于 64KB 缓冲），比逐块扫描更稳 */
    r = (u8)((f_read(&fp, buf, Camera_JpegBufSize(), &br) == FR_OK) ? 0 : 6);
    f_close(&fp);

    if (r != 0)
    {
        return r;
    }

    n = br;
    Jpeg_DbgLen = n;

    if (n < 4)
    {
        return 5;
    }

    Jpeg_DbgHdr = (u16)(((u16)buf[0] << 8) | buf[1]);
    Jpeg_DbgTail = (u16)(((u16)buf[n - 2] << 8) | buf[n - 1]);

    /* 跳过帧头前的无效字节，定位到 SOI(FFD8) */
    while (((off + 1) < n) && !((buf[off] == 0xFF) && (buf[off + 1] == 0xD8)))
    {
        off++;
    }
    if ((off + 1) >= n)
    {
        return 5;
    }

    s_ox = x;
    s_oy = y;

    /* 进入可打断解码：每画一行检查按键，有键按下立即中止 */
    Jpeg_Aborted  = 0;
    Jpeg_KeyEvent = 0;
    r = decode_buffer(buf + off, n - off, max_w, max_h);

    if (Jpeg_Aborted)
    {
        return 30;                      /* 被按键打断（不是错误） */
    }
    return r;
}

u8 Jpeg_PlayVideo(const char *path, u16 x, u16 y, u16 max_w, u16 max_h)
{
    FIL  fp;
    u32  n;
    u8  *buf = Camera_BufPtr();

    Jpeg_Aborted  = 0;
    Jpeg_KeyEvent = 0;

    if (buf == 0)
    {
        return 4;
    }

    SDIO_PinsConfig();          /* 读文件前先抢回 SDIO 数据线 */

    if (f_open(&fp, (const TCHAR *)path, FA_READ) != FR_OK)
    {
        return 3;
    }

    while (!Jpeg_Aborted)
    {
        u32 t0;

        n = extract_frame(&fp, buf, Camera_JpegBufSize());
        if (n < 4)
        {
            break;                         /* 文件结束 */
        }

        s_ox = x;
        s_oy = y;

        t0 = HAL_GetTick();
        if (decode_buffer(buf, n, max_w, max_h) != 0)
        {
            break;                         /* 被按键打断 / 帧损坏 */
        }

        /* 限速 ~8fps；期间按键由 out_lcd 检测并中止 */
        while ((HAL_GetTick() - t0) < 110U)
        {
            if (Jpeg_Aborted || KEY_AnyDown())
            {
                key_dir_t kd = KEY_GetDir();

                if (kd != KEY_DIR_NONE)
                {
                    Jpeg_KeyEvent = (u8)kd;
                }
                Jpeg_Aborted = 1;
                break;
            }
        }
    }

    f_close(&fp);
    return 0;
}
