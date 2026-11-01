/**
 ******************************************************************************
 * @file    camera.h
 * @brief   拍照 / 录像用的 JPEG 采集接口
 * @note    RGB565 实时预览走的是 DCMI->DMA->LCD GRAM；
 *          拍照/录像需要拿到完整一帧数据，所以切到 OV2640 的 JPEG 输出，
 *          走 DCMI->DMA->RAM 缓冲，取到一帧后再写文件。
 ******************************************************************************
 */
#ifndef __CAMERA_H
#define __CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* 切到 JPEG 模式并准备好缓冲区，w/h 为输出分辨率(4 的倍数) */
u8  Camera_JpegBegin(u16 w, u16 h);

/* 采一帧完整 JPEG，成功返回字节数并把 *buf 指向数据；失败返回 0 */
u32 Camera_JpegGrab(const u8 **buf, u32 timeout_ms);

/* 退出 JPEG 模式，恢复 RGB565（调用方随后重启实时预览） */
void Camera_JpegEnd(u16 preview_w, u16 preview_h);

/* JPEG 缓冲区大小(字节) */
u32 Camera_JpegBufSize(void);

/* JPEG 缓冲区首地址（文件浏览时复用来存放待解码的一帧） */
u8 *Camera_BufPtr(void);

#ifdef __cplusplus
}
#endif

#endif /* __CAMERA_H */
