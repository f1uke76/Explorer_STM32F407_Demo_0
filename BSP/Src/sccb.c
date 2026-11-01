/**
 ******************************************************************************
 * @file    sccb.c
 * @brief   OV2640 SCCB 总线时序（GPIO 模拟）
 * @note    时序与 I2C 兼容，但 OV 器件在每字节后使用"无应答/应答"位，
 *          读操作分为两段：先写寄存器地址，再重新起始读数据。
 ******************************************************************************
 */
#include "sccb.h"
#include "bsp_delay.h"

/* 初始化 SCCB 引脚：PD6(SCL)、PD7(SDA)，推挽输出 */
void SCCB_Init(void)
{
    GPIO_InitTypeDef GPIO_Initure;

    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_Initure.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_Initure.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_Initure.Pull  = GPIO_PULLUP;
    GPIO_Initure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOD, &GPIO_Initure);

    SCCB_SDA_OUT();
}

/* 起始信号：SCL 为高时 SDA 由高变低 */
void SCCB_Start(void)
{
    SCCB_SDA_H();
    SCCB_SCL_H();
    delay_us(50);
    SCCB_SDA_L();
    delay_us(50);
    SCCB_SCL_L();
}

/* 停止信号：SCL 为高时 SDA 由低变高 */
void SCCB_Stop(void)
{
    SCCB_SDA_L();
    delay_us(50);
    SCCB_SCL_H();
    delay_us(50);
    SCCB_SDA_H();
    delay_us(50);
}

/* 产生 NA（非应答）信号 */
void SCCB_No_Ack(void)
{
    delay_us(50);
    SCCB_SDA_H();
    SCCB_SCL_H();
    delay_us(50);
    SCCB_SCL_L();
    delay_us(50);
    SCCB_SDA_L();
    delay_us(50);
}

/* 写一个字节，返回 0 表示从机应答 */
u8 SCCB_WR_Byte(u8 dat)
{
    u8 j, res;

    for (j = 0; j < 8; j++)
    {
        if (dat & 0x80) SCCB_SDA_H();
        else            SCCB_SDA_L();
        dat <<= 1;
        delay_us(50);
        SCCB_SCL_H();
        delay_us(50);
        SCCB_SCL_L();
    }

    SCCB_SDA_IN();          /* 释放 SDA，读应答位 */
    delay_us(50);
    SCCB_SCL_H();
    delay_us(50);
    res = (SCCB_READ_SDA() != 0) ? 1 : 0;
    SCCB_SCL_L();
    SCCB_SDA_OUT();

    return res;
}

/* 读一个字节（SCL 上升沿锁存） */
u8 SCCB_RD_Byte(void)
{
    u8 temp = 0, j;

    SCCB_SDA_IN();
    for (j = 8; j > 0; j--)
    {
        delay_us(50);
        SCCB_SCL_H();
        temp <<= 1;
        if (SCCB_READ_SDA()) temp++;
        delay_us(50);
        SCCB_SCL_L();
    }
    SCCB_SDA_OUT();

    return temp;
}

/* 写寄存器，返回 0 表示成功 */
u8 SCCB_WR_Reg(u8 reg, u8 data)
{
    u8 res = 0;

    SCCB_Start();
    if (SCCB_WR_Byte(SCCB_ID)) res = 1;
    delay_us(100);
    if (SCCB_WR_Byte(reg))     res = 1;
    delay_us(100);
    if (SCCB_WR_Byte(data))    res = 1;
    SCCB_Stop();

    return res;
}

/* 读寄存器 */
u8 SCCB_RD_Reg(u8 reg)
{
    u8 val = 0;

    SCCB_Start();
    SCCB_WR_Byte(SCCB_ID);
    delay_us(100);
    SCCB_WR_Byte(reg);
    delay_us(100);
    SCCB_Stop();
    delay_us(100);

    SCCB_Start();
    SCCB_WR_Byte(SCCB_ID | 0x01);
    delay_us(100);
    val = SCCB_RD_Byte();
    SCCB_No_Ack();
    SCCB_Stop();

    return val;
}
