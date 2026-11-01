/**
 ******************************************************************************
 * @file    camera.c
 * @brief   OV2640 JPEG 采集（拍照 / 录像取帧）
 * @note    缓冲区放在内部 RAM（64KB）。一帧 JPEG 通常 10~25KB，
 *          足够容纳 480x360 的输出。
 ******************************************************************************
 */
#include <string.h>
#include "camera.h"
#include "dcmi.h"
#include "ov2640.h"
#include "bsp_delay.h"
#include "key.h"

#define JPEG_BUF_WORDS   16384U          /* 16384 x 4B = 64KB */

static u32 s_jpeg_buf[JPEG_BUF_WORDS] __attribute__((aligned(4)));

u32 Camera_JpegBufSize(void)
{
    return JPEG_BUF_WORDS * 4U;
}

/* 暴露缓冲区给文件浏览/图片解码复用（此时摄像头不在采集，缓冲区空闲） */
u8 *Camera_BufPtr(void)
{
    return (u8 *)s_jpeg_buf;
}

u8 Camera_JpegBegin(u16 w, u16 h)
{
    if ((w % 4) || (h % 4))
    {
        return 1;
    }

    OV2640_JPEG_Mode();                  /* 切到 YUV422 + JPEG 输出 */
    OV2640_OutSize_Set(w, h);            /* 输出分辨率 */
    DCMI_Init();                         /* 复位 DCMI 状态机(清除残留配置) */

    memset(s_jpeg_buf, 0, sizeof(s_jpeg_buf));

    DCMI_JpegDMA_Init(s_jpeg_buf, (u16)JPEG_BUF_WORDS);

    return 0;
}

/**
 * @brief 在缓冲区里找 JPEG 的 EOI(FFD9)，返回真实帧长度
 * @return 0 表示没找到（帧不完整）
 * @note  JPEG 熵编码里 0xFF 会被转义成 0xFF00，所以第一个 FFD9 就是 EOI。
 *        不再依赖 DMA 计数器换算的长度，避免长度偏小导致帧尾被截掉。
 */
static u32 jpeg_find_eoi(const u8 *p, u32 limit)
{
    u32 k;

    if (limit < 4)
    {
        return 0;
    }

    for (k = 2; (k + 1) < limit; k++)
    {
        if ((p[k] == 0xFF) && (p[k + 1] == 0xD9))
        {
            return k + 2;
        }
    }
    return 0;
}

/**
 * @brief 采一帧完整 JPEG
 * @note  启动采集时相机可能处在帧中间，前几帧可能是半帧；官方例程丢 2 帧取第 3 帧。
 *        这里改成：每采到一帧就在缓冲区里找 EOI(FFD9)，找到才算完整，最多试 5 帧。
 */
u32 Camera_JpegGrab(const u8 **buf, u32 timeout_ms)
{
    u32 t0  = HAL_GetTick();
    u8  attempt;
    u32 last_len = 0;

    if (buf == 0)
    {
        return 0;
    }

    for (attempt = 0; attempt < 5; attempt++)
    {
        u8  done = 0;
        u32 limit, real;

        /* 每次重采前清空缓冲区，避免上一帧的残留数据里出现假的 FFD9 */
        memset(s_jpeg_buf, 0, sizeof(s_jpeg_buf));

        DCMI_JpegStart();

        while ((HAL_GetTick() - t0) < timeout_ms)
        {
            if (jpeg_frame_ready)
            {
                jpeg_frame_ready = 0;
                last_len = jpeg_frame_len;
                done = 1;
                break;
            }

            /* 有键按下立刻退出，保证录像时按键能及时响应 */
            if (KEY_AnyDown())
            {
                break;
            }
        }

        if (!done)
        {
            break;                                  /* 超时 */
        }

        /* 必须以 SOI(FFD8) 开头，否则是半帧，重采 */
        {
            const u8 *p = (const u8 *)s_jpeg_buf;

            if ((p[0] != 0xFF) || (p[1] != 0xD8))
            {
                continue;
            }
        }

        /* 在 DMA 报告长度之外再多留 4KB 余量，找真实 EOI */
        limit = last_len + 4096U;
        if (limit > Camera_JpegBufSize())
        {
            limit = Camera_JpegBufSize();
        }

        real = jpeg_find_eoi((const u8 *)s_jpeg_buf, limit);
        if (real > 4)
        {
            *buf = (const u8 *)s_jpeg_buf;
            return real;                            /* 完整一帧 */
        }
        /* 没找到 EOI，说明这帧不完整，继续采下一帧 */
    }

    DCMI_Stop();

    DCMI_Stop();

    /* 没拿到完整帧（超时/被按键打断）就返回 0，
     * 不能返回半截数据：缓冲区可能已被下一轮 memset 清空 */
    return 0;
}

void Camera_JpegEnd(u16 preview_w, u16 preview_h)
{
    DCMI_Stop();

    cam_mode = 0;

    /* 恢复到 RGB565 预览：先切回传感器输出格式，再重设输出尺寸。
     * DCMI 的复位交给 preview_start_stream() 统一处理，避免重复初始化。 */
    OV2640_RGB565_Mode();
    OV2640_OutSize_Set(preview_w, preview_h);
}
