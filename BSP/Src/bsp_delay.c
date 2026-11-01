/**
 ******************************************************************************
 * @file    bsp_delay.c
 * @brief   DWT 延时实现
 * @note    DWT->CYCCNT 每个内核时钟周期加 1，168MHz 下 1us = 168 个周期。
 *          用寄存器直接操作，不依赖 HAL 的 SysTick，中断关闭时同样可用。
 ******************************************************************************
 */
#include "bsp_delay.h"

void bsp_delay_init(void)
{
    /* 使能 DWT 及周期计数器 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);

    while ((DWT->CYCCNT - start) < ticks)
    {
        /* 等待 */
    }
}

void delay_ms(uint32_t ms)
{
    while (ms--)
    {
        delay_us(1000U);
    }
}
