/**
 ******************************************************************************
 * @file    key.c
 * @brief   板载按键：WK_UP(PA0 高有效)、KEY0/1/2(PE4/PE3/PE2 低有效)
 ******************************************************************************
 */
#include "key.h"
#include "bsp_delay.h"

void KEY_Init(void)
{
    GPIO_InitTypeDef GPIO_Initure;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* PA0: WK_UP，外部下拉，按下为高电平 */
    GPIO_Initure.Pin   = GPIO_PIN_0;
    GPIO_Initure.Mode  = GPIO_MODE_INPUT;
    GPIO_Initure.Pull  = GPIO_PULLDOWN;
    GPIO_Initure.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_Initure);

    /* PE2/PE3/PE4: KEY2/KEY1/KEY0，外部上拉，按下为低电平 */
    GPIO_Initure.Pin   = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;
    GPIO_Initure.Mode  = GPIO_MODE_INPUT;
    GPIO_Initure.Pull  = GPIO_PULLUP;
    GPIO_Initure.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &GPIO_Initure);
}

/* 按键扫描，返回 0 表示无按键按下；优先级 KEY0 > KEY1 > KEY2 > WK_UP */
u8 KEY_Scan(u8 mode)
{
    static u8 key_up = 1;   /* 按键松开标志 */

    if (mode == 1)
    {
        key_up = 1;         /* 支持连按 */
    }

    if (key_up && ((KEY0 == 0) || (KEY1 == 0) || (KEY2 == 0) || (WK_UP == 1)))
    {
        delay_ms(10);       /* 消抖 */
        key_up = 0;

        if (KEY0 == 0)      return KEY0_PRES;
        else if (KEY1 == 0) return KEY1_PRES;
        else if (KEY2 == 0) return KEY2_PRES;
        else if (WK_UP == 1) return WKUP_PRES;
    }
    else if ((KEY0 == 1) && (KEY1 == 1) && (KEY2 == 1) && (WK_UP == 0))
    {
        key_up = 1;
    }

    return 0;
}

/**
 * @brief 读取方向键事件（消抖、不连发）
 * @retval KEY_DIR_NONE / UP / DOWN / LEFT / RIGHT
 * @note   优先级 KEY0(右) > KEY1(下) > KEY2(左) > WK_UP(上)
 */
/* 读当前按下的方向(不做消抖) */
static key_dir_t key_read_dir(void)
{
    if (KEY0 == 0)       return KEY_DIR_RIGHT;
    else if (KEY1 == 0)  return KEY_DIR_DOWN;
    else if (KEY2 == 0)  return KEY_DIR_LEFT;
    else if (WK_UP == 1) return KEY_DIR_UP;
    return KEY_DIR_NONE;
}

/**
 * @brief 读方向键事件
 * @param repeat 1: 长按连发(按下先给一次，之后每 120ms 再来一次)
 *               0: 不连发(按下只给一次)
 */
key_dir_t KEY_GetDirEx(u8 repeat)
{
    static u8  key_up    = 1;
    static u32 press_tick = 0;
    static u32 last_emit  = 0;

    if (key_up)
    {
        if (key_read_dir() != KEY_DIR_NONE)
        {
            delay_ms(10);                       /* 消抖 */
            if (key_read_dir() == KEY_DIR_NONE)
            {
                return KEY_DIR_NONE;            /* 抖动，忽略 */
            }
            key_up     = 0;
            press_tick = HAL_GetTick();
            last_emit  = press_tick;
            return key_read_dir();              /* 首次按下立刻响应 */
        }
    }
    else
    {
        if (key_read_dir() == KEY_DIR_NONE)
        {
            key_up = 1;                         /* 松开，允许下一次触发 */
        }
        else if (repeat)
        {
            u32 now = HAL_GetTick();

            if ((now - press_tick) >= 400U)     /* 按住超过 400ms 开始连发 */
            {
                if ((now - last_emit) >= 120U)  /* 之后每 120ms 一次 */
                {
                    last_emit = now;
                    return key_read_dir();
                }
            }
        }
    }

    return KEY_DIR_NONE;
}

key_dir_t KEY_GetDir(void)
{
    return KEY_GetDirEx(0);
}

/* 任意键当前是否按下：只读电平、不消抖、不改变 KEY_GetDir 的内部状态。
 * 用于长耗时操作(如图像解码)中快速判断是否需要中断。 */
u8 KEY_AnyDown(void)
{
    if ((KEY0 == 0) || (KEY1 == 0) || (KEY2 == 0) || (WK_UP == 1))
    {
        return 1;
    }
    return 0;
}
