/**
 ******************************************************************************
 * @file    lcd.h
 * @brief   ALIENTEK 探索者 STM32F407 开发板 TFT-LCD 驱动（FSMC 接口）
 *          支持驱动 IC：ILI9341 / NT35310 / NT35510 / ST7789 / SSD1963
 * @note    硬件连接（见 Explorer STM32F4_V2.2 原理图）：
 *            FSMC Bank1 NE4 (PG12)   -> LCD 片选 CS
 *            FSMC A6      (PF12)     -> LCD 数据/命令选择 RS
 *            FSMC D0~D15 (PD/PE)     -> LCD 16 位数据总线
 *            FSMC NOE/NWE (PD4/PD5)  -> LCD 读/写
 *            PB15                    -> LCD 背光 BL
 ******************************************************************************
 */
#ifndef __LCD_H
#define __LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdlib.h>
#include "bsp_delay.h"

/* 兼容 ALIENTEK 原有代码风格的整数类型 */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef volatile uint8_t  vu8;
typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;

extern SRAM_HandleTypeDef TFTSRAM_Handler;  /* SRAM 句柄(用于控制 LCD) */

/* LCD 重要参数 */
typedef struct
{
    u16 width;      /* LCD 宽度 */
    u16 height;     /* LCD 高度 */
    u16 id;         /* LCD ID */
    u8  dir;        /* 横竖屏控制：0 竖屏，1 横屏 */
    u16 wramcmd;    /* 开始写 gram 指令 */
    u16 setxcmd;    /* 设置 x 坐标指令 */
    u16 setycmd;    /* 设置 y 坐标指令 */
} _lcd_dev;

extern _lcd_dev lcddev;   /* 管理 LCD 重要参数 */
extern u32 POINT_COLOR;   /* 画笔颜色，默认红色 */
extern u32 BACK_COLOR;    /* 背景色，默认白色 */

/* ---------------- MCU 与 LCD 端口定义 ---------------- */
#define LCD_LED_ON()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET)    /* 背光开 */
#define LCD_LED_OFF()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET)  /* 背光关 */

/* LCD 地址结构体：同一片选内，A6=0 写命令，A6=1 写数据 */
typedef struct
{
    vu16 LCD_REG;
    vu16 LCD_RAM;
} LCD_TypeDef;

/* 使用 NOR/SRAM Bank1.sector4，地址位 HADDR[27:26]=11，A6 作为数据/命令选择
 * 注意 STM32 内部地址会右移一位：111 1110 = 0x7E */
#define LCD_BASE        ((u32)(0x6C000000 | 0x0000007E))
#define LCD             ((LCD_TypeDef *)LCD_BASE)

/* 扫描方向 */
#define L2R_U2D  0      /* 从左到右,从上到下 */
#define L2R_D2U  1      /* 从左到右,从下到上 */
#define R2L_U2D  2      /* 从右到左,从上到下 */
#define R2L_D2U  3      /* 从右到左,从下到上 */
#define U2D_L2R  4      /* 从上到下,从左到右 */
#define U2D_R2L  5      /* 从上到下,从右到左 */
#define D2U_L2R  6      /* 从下到上,从左到右 */
#define D2U_R2L  7      /* 从下到上,从右到左 */

#define DFT_SCAN_DIR  L2R_U2D   /* 默认的扫描方向 */

/* 常用颜色 (RGB565) */
#define WHITE           0xFFFF
#define BLACK           0x0000
#define BLUE            0x001F
#define BRED            0XF81F
#define GRED            0XFFE0
#define GBLUE           0X07FF
#define RED             0xF800
#define MAGENTA         0xF81F
#define GREEN           0x07E0
#define CYAN            0x7FFF
#define YELLOW          0xFFE0
#define BROWN           0XBC40
#define BRRED           0XFC07
#define GRAY            0X8430
/* GUI 颜色 */
#define DARKBLUE        0X01CF
#define LIGHTBLUE       0X7D7C
#define GRAYBLUE        0X5458
#define LIGHTGREEN      0X841F
#define LGRAY           0XC618
#define LGRAYBLUE       0XA651
#define LBBLUE          0X2B12

/* ---------------- 对外接口 ---------------- */
void LCD_Init(void);                                                    /* 初始化 */
void LCD_DisplayOn(void);                                               /* 开启显示 */
void LCD_DisplayOff(void);                                              /* 关闭显示 */
void LCD_Clear(u32 Color);                                              /* 清屏 */
void LCD_SetCursor(u16 Xpos, u16 Ypos);                                 /* 设置光标 */
void LCD_DrawPoint(u16 x, u16 y);                                       /* 画点 */
void LCD_Fast_DrawPoint(u16 x, u16 y, u32 color);                       /* 快速画点 */
u32  LCD_ReadPoint(u16 x, u16 y);                                       /* 读点 */
void LCD_Draw_Circle(u16 x0, u16 y0, u8 r);                             /* 画圆 */
void LCD_DrawLine(u16 x1, u16 y1, u16 x2, u16 y2);                      /* 画线 */
void LCD_DrawRectangle(u16 x1, u16 y1, u16 x2, u16 y2);                 /* 画矩形 */
void LCD_Fill(u16 sx, u16 sy, u16 ex, u16 ey, u32 color);               /* 填充单色 */
void LCD_Color_Fill(u16 sx, u16 sy, u16 ex, u16 ey, u16 *color);        /* 填充指定颜色 */
void LCD_ShowChar(u16 x, u16 y, u8 num, u8 size, u8 mode);              /* 显示一个字符 */
void LCD_ShowNum(u16 x, u16 y, u32 num, u8 len, u8 size);               /* 显示一个数字 */
void LCD_ShowxNum(u16 x, u16 y, u32 num, u8 len, u8 size, u8 mode);     /* 显示数字(带前导) */
void LCD_ShowString(u16 x, u16 y, u16 width, u16 height, u8 size, const u8 *p); /* 显示字符串,12/16/24/32 号字 */

void LCD_WriteReg(u16 LCD_Reg, u16 LCD_RegValue);
u16  LCD_ReadReg(u16 LCD_Reg);
void LCD_WriteRAM_Prepare(void);
void LCD_WriteRAM(u16 RGB_Code);
void LCD_SSD_BackLightSet(u8 pwm);                                      /* SSD1963 背光设置 */
void LCD_Scan_Dir(u8 dir);                                              /* 设置扫描方向 */
void LCD_Display_Dir(u8 dir);                                           /* 设置屏幕显示方向 */
void LCD_Set_Window(u16 sx, u16 sy, u16 width, u16 height);             /* 设置窗口 */

/* LCD 分辨率参数（SSD1963 用） */
#define SSD_HOR_RESOLUTION      800
#define SSD_VER_RESOLUTION      480
#define SSD_HOR_PULSE_WIDTH     1
#define SSD_HOR_BACK_PORCH      46
#define SSD_HOR_FRONT_PORCH     210
#define SSD_VER_PULSE_WIDTH     1
#define SSD_VER_BACK_PORCH      23
#define SSD_VER_FRONT_PORCH     22
#define SSD_HT                  (SSD_HOR_RESOLUTION + SSD_HOR_BACK_PORCH + SSD_HOR_FRONT_PORCH)
#define SSD_HPS                 (SSD_HOR_BACK_PORCH)
#define SSD_VT                  (SSD_VER_RESOLUTION + SSD_VER_BACK_PORCH + SSD_VER_FRONT_PORCH)
#define SSD_VPS                 (SSD_VER_BACK_PORCH)

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H */
