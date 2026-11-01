/**
 ******************************************************************************
 * @file    key.h
 * @brief   探索者 STM32F407 板载按键驱动（4 键组成方向键）
 * @note    方向键映射（按用户指定）：
 *            上 = WK_UP (PA0，按下为高)
 *            左 = KEY2  (PE2，按下为低)
 *            下 = KEY1  (PE3，按下为低)
 *            右 = KEY0  (PE4，按下为低)
 ******************************************************************************
 */
#ifndef __KEY_H
#define __KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;

#define KEY0        HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4)
#define KEY1        HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3)
#define KEY2        HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_2)
#define WK_UP       HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0)

/* 原始键值 */
#define KEY0_PRES   1
#define KEY1_PRES   2
#define KEY2_PRES   3
#define WKUP_PRES   4

/* 方向键（UI 语义） */
typedef enum
{
    KEY_DIR_NONE = 0,
    KEY_DIR_UP,      /* WK_UP */
    KEY_DIR_DOWN,    /* KEY1  */
    KEY_DIR_LEFT,    /* KEY2  */
    KEY_DIR_RIGHT    /* KEY0  */
} key_dir_t;

void      KEY_Init(void);
u8        KEY_Scan(u8 mode);    /* 返回原始键值, 0 表示无按键 */
key_dir_t KEY_GetDir(void);     /* 返回方向键事件(消抖, 不连发) */
key_dir_t KEY_GetDirEx(u8 repeat); /* repeat=1 时长按连发 */
u8   KEY_AnyDown(void);         /* 任意键当前是否按下(原始电平, 不消抖, 不消耗事件) */

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H */
