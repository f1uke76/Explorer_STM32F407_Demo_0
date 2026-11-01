/**
 ******************************************************************************
 * @file    dcmi.h
 * @brief   DCMI 摄像头并口采集驱动（OV2640 -> DMA -> LCD GRAM）
 * @note    探索者 STM32F407 摄像头接口引脚（AF13）：
 *            PA4  HSYNC      PA6  PCLK       PB7  VSYNC
 *            PC6  D0         PC7  D1         PC8  D2        PC9  D3
 *            PC11 D4         PB6  D5         PE5  D6        PE6  D7
 *            PG9  PWDN       PG15 RESET      PD6/PD7 SCCB
 ******************************************************************************
 */
#ifndef __DCMI_H
#define __DCMI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

extern DCMI_HandleTypeDef DCMI_Handler;        /* DCMI 句柄 */
extern DMA_HandleTypeDef  DMADMCI_Handler;     /* DMA 句柄 */
extern volatile u32       dcmi_frame_count;    /* 已采集帧数 */
extern volatile u8        dcmi_frame_edge;     /* 1: 刚到一帧结束(主循环清零) */
extern volatile u8        cam_mode;            /* 0: RGB565->LCD  1: JPEG->RAM */
extern volatile u8        jpeg_frame_ready;    /* JPEG 一帧采集完成 */
extern volatile u32       jpeg_frame_len;      /* JPEG 本帧字节数 */

void DCMI_Init(void);
void DCMI_PinsConfig(void);      /* 把 PC8/9/11 从 SDIO 抢回 DCMI */
void DCMI_DMA_Init(u32 memaddr, u16 memsize, u32 memblen, u32 meminc);
void DCMI_JpegDMA_Init(u32 *buf, u16 words);
void DCMI_JpegStart(void);
void DCMI_Start(void);
void DCMI_Stop(void);
void DCMI_Set_Window(u16 sx, u16 sy, u16 width, u16 height);

#ifdef __cplusplus
}
#endif

#endif /* __DCMI_H */
