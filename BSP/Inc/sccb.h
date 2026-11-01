/**
 ******************************************************************************
 * @file    sccb.h
 * @brief   OV 系列摄像头 SCCB（类 I2C）总线，GPIO 模拟时序
 * @note    探索者 STM32F407 摄像头接口：
 *            SCCB_SCL -> PD6
 *            SCCB_SDA -> PD7（双向，读写时需切换输入/输出）
 *            器件地址 0x60（OV2640 写地址），读地址 0x61
 ******************************************************************************
 */
#ifndef __SCCB_H
#define __SCCB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef uint8_t u8;

/* SDA 方向切换（PD7） */
#define SCCB_SDA_IN()   do { GPIOD->MODER &= ~(3U << (7 * 2)); GPIOD->MODER |= (0U << (7 * 2)); } while (0)
#define SCCB_SDA_OUT()  do { GPIOD->MODER &= ~(3U << (7 * 2)); GPIOD->MODER |= (1U << (7 * 2)); } while (0)

/* 引脚电平操作 */
#define SCCB_SCL_H()    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6, GPIO_PIN_SET)
#define SCCB_SCL_L()    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6, GPIO_PIN_RESET)
#define SCCB_SDA_H()    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_SET)
#define SCCB_SDA_L()    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_RESET)
#define SCCB_READ_SDA() HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_7)

#define SCCB_ID         0x60    /* OV2640 器件地址 */

void SCCB_Init(void);
void SCCB_Start(void);
void SCCB_Stop(void);
void SCCB_No_Ack(void);
u8   SCCB_WR_Byte(u8 dat);
u8   SCCB_RD_Byte(void);
u8   SCCB_WR_Reg(u8 reg, u8 data);
u8   SCCB_RD_Reg(u8 reg);

#ifdef __cplusplus
}
#endif

#endif /* __SCCB_H */
