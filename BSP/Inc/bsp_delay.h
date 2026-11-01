/**
 ******************************************************************************
 * @file    bsp_delay.h
 * @brief   基于 DWT 内核周期计数器的微秒/毫秒级延时（不占用定时器）
 ******************************************************************************
 */
#ifndef __BSP_DELAY_H
#define __BSP_DELAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

void bsp_delay_init(void);       /* 初始化 DWT，需在系统时钟配置完成后调用 */
void delay_us(uint32_t us);      /* 微秒级延时 */
void delay_ms(uint32_t ms);      /* 毫秒级延时 */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_DELAY_H */
