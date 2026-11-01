/**
 ******************************************************************************
 * @file    jpeg_view.h
 * @brief   用 TJpgDec 解码 JPEG 并在 LCD 上显示（图片浏览 / MJPEG 播放）
 ******************************************************************************
 */
#ifndef __JPEG_VIEW_H
#define __JPEG_VIEW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* 在 (x,y) 起、max_w x max_h 区域内居中显示一个 JPEG 文件；0:成功 */
u8 Jpeg_ShowFile(const char *path, u16 x, u16 y, u16 max_w, u16 max_h);

/* 播放 MJPEG(连续 JPEG 帧)文件，直到文件结束或按下任意方向键；0:正常结束 */
u8 Jpeg_PlayVideo(const char *path, u16 x, u16 y, u16 max_w, u16 max_h);

/* 诊断：最近一次 Jpeg_ShowFile 读到的字节数与文件头/尾两字节(大端) */
extern volatile u32 Jpeg_DbgLen;
extern volatile u16 Jpeg_DbgHdr;
extern volatile u16 Jpeg_DbgTail;

/* 解码被按键打断时捕获到的方向(0=无)，由主循环取走处理 */
extern volatile u8  Jpeg_KeyEvent;

#ifdef __cplusplus
}
#endif

#endif /* __JPEG_VIEW_H */
