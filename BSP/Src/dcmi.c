/**
 ******************************************************************************
 * @file    dcmi.c
 * @brief   DCMI + DMA 摄像头采集驱动
 *
 * @note    实时预览原理（ALIENTEK 经典方案，不需要外部 SRAM）：
 *          1) DMA2_Stream1_Channel1 把 DCMI->DR 的数据搬到 LCD 的 GRAM 地址
 *             (&LCD->LCD_RAM)，存储器地址不递增、循环模式，相当于数据流不断
 *             灌进显存，由 LCD 控制器自己递增 GRAM 地址；
 *          2) DCMI 帧中断到来时把 GRAM 写指针复位到 (0,0)，保证每帧从屏幕
 *             左上角开始铺满，画面才不会滚动；
 *          3) 因为一个像素是 16bit，而 DCMI->DR 一次读出 32bit(两个像素)，
 *             所以 DMA 必须开启 FIFO 做 32bit -> 16bit 的位宽转换。
 ******************************************************************************
 */
#include "dcmi.h"
#include "lcd.h"
#include "ov2640.h"

DCMI_HandleTypeDef DCMI_Handler;
DMA_HandleTypeDef  DMADMCI_Handler;
volatile u32       dcmi_frame_count = 0;
volatile u8        dcmi_frame_edge  = 0;   /* 帧结束标志，由主循环消费 */

volatile u8        cam_mode        = 0;    /* 0: RGB565->LCD  1: JPEG->RAM */
volatile u8        jpeg_frame_ready = 0;
volatile u32       jpeg_frame_len   = 0;
static   u32       jpeg_words       = 0;    /* JPEG 缓冲区字数(32bit) */

/* DCMI 中断服务函数 */
void DCMI_IRQHandler(void)
{
    HAL_DCMI_IRQHandler(&DCMI_Handler);
}

/**
 * @brief 一帧图像采集完成回调（帧中断）
 * @note  1) 必须先把 DMA 停掉再发 CASET/RASET/RAMWR 命令：
 *           像素流还在往 LCD 数据口灌数据时发命令，命令参数会被紧随其后的
 *           像素字节污染，窗口寄存器写坏 → 整屏花屏。
 *        2) 这里正好是相机消隐期，停/开 DMA 不会丢像素。
 *        3) 每帧把写指针复位到 (0,0)：若某次刷新文本时丢了几个像素，
 *           下一帧会自动重新对齐，不会累积偏移。
 */
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
    __HAL_DCMI_CLEAR_FLAG(&DCMI_Handler, DCMI_FLAG_FRAMERI);

    if (cam_mode == 0)
    {
        /* ---- RGB565 实时预览：写指针复位到预览区左上角 ---- */
        __HAL_DMA_DISABLE(&DMADMCI_Handler);
        LCD_SetCursor(0, 0);
        LCD_WriteRAM_Prepare();
        __HAL_DMA_ENABLE(&DMADMCI_Handler);
    }
    else
    {
        /* ---- JPEG 采集：停 DMA，算出本帧字节数 ---- */
        u32 guard = 10000U;

        __HAL_DMA_DISABLE(&DMADMCI_Handler);
        while ((DMADMCI_Handler.Instance->CR & DMA_SxCR_EN) && (--guard))
        {
            /* 等 DMA 真正停止，避免读到还在变的计数器 */
        }
        jpeg_frame_len   = (jpeg_words - __HAL_DMA_GET_COUNTER(&DMADMCI_Handler)) * 4U;
        jpeg_frame_ready = 1;
    }

    dcmi_frame_count++;
    dcmi_frame_edge = 1;

    __HAL_DCMI_ENABLE_IT(&DCMI_Handler, DCMI_IT_FRAME);
}

/**
 * @brief DCMI 的 DMA 配置
 * @param memaddr 目标地址（可以是 LCD->LCD_RAM，也可以是内存缓冲区）
 * @param memsize 传输长度（循环模式下可设为 1，实现自由流动）
 * @param memblen 存储器数据宽度 DMA_MDATAALIGN_BYTE/HALFWORD/WORD
 * @param meminc  存储器地址是否递增 DMA_MINC_ENABLE/DMA_MINC_DISABLE
 */
void DCMI_DMA_Init(u32 memaddr, u16 memsize, u32 memblen, u32 meminc)
{
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_LINKDMA(&DCMI_Handler, DMA_Handle, DMADMCI_Handler);

    DMADMCI_Handler.Instance                 = DMA2_Stream1;
    DMADMCI_Handler.Init.Channel             = DMA_CHANNEL_1;
    DMADMCI_Handler.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    DMADMCI_Handler.Init.PeriphInc           = DMA_PINC_DISABLE;
    DMADMCI_Handler.Init.MemInc              = meminc;
    DMADMCI_Handler.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;   /* DCMI->DR 是 32bit(2 像素) */
    DMADMCI_Handler.Init.MemDataAlignment    = memblen;
    DMADMCI_Handler.Init.Mode                = DMA_CIRCULAR;
    DMADMCI_Handler.Init.Priority            = DMA_PRIORITY_HIGH;
    DMADMCI_Handler.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;   /* 必须开 FIFO 才能做位宽转换 */
    DMADMCI_Handler.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_HALFFULL;
    DMADMCI_Handler.Init.MemBurst            = DMA_MBURST_SINGLE;
    DMADMCI_Handler.Init.PeriphBurst         = DMA_PBURST_SINGLE;

    HAL_DMA_DeInit(&DMADMCI_Handler);
    HAL_DMA_Init(&DMADMCI_Handler);

    /* HAL_DMA_Start 内部会 __HAL_LOCK，若句柄处于锁定状态会直接返回 HAL_BUSY，
       导致后续配置被跳过，这里先解锁一次 */
    __HAL_UNLOCK(&DMADMCI_Handler);

    HAL_DMA_Start(&DMADMCI_Handler, (u32)&DCMI->DR, memaddr, memsize);
}

/* DCMI 初始化 */
void DCMI_Init(void)
{
    DCMI_PinsConfig();          /* 先确保 PC8/9/11 在 DCMI 上(SDIO 可能占用) */

    DCMI_Handler.Instance                = DCMI;
    DCMI_Handler.Init.SynchroMode        = DCMI_SYNCHRO_HARDWARE;     /* 硬件同步 HSYNC/VSYNC */
    DCMI_Handler.Init.PCKPolarity        = DCMI_PCKPOLARITY_RISING;   /* PCLK 上升沿有效 */
    DCMI_Handler.Init.VSPolarity         = DCMI_VSPOLARITY_LOW;       /* VSYNC 低电平有效 */
    DCMI_Handler.Init.HSPolarity         = DCMI_HSPOLARITY_LOW;       /* HSYNC 低电平有效 */
    DCMI_Handler.Init.CaptureRate        = DCMI_CR_ALL_FRAME;         /* 全帧捕获 */
    DCMI_Handler.Init.ExtendedDataMode   = DCMI_EXTEND_DATA_8B;       /* 8 位数据模式 */
    HAL_DCMI_Init(&DCMI_Handler);

    /* HAL_DCMI_Init 会默认打开一堆中断，这里先全部关掉，只留帧中断 */
    DCMI->IER = 0x0;

    __HAL_DCMI_ENABLE_IT(&DCMI_Handler, DCMI_IT_FRAME);
    __HAL_DCMI_ENABLE(&DCMI_Handler);
}

/* DCMI 底层初始化：时钟、引脚、中断（由 HAL_DCMI_Init 调用） */
void HAL_DCMI_MspInit(DCMI_HandleTypeDef *hdcmi)
{
    GPIO_InitTypeDef GPIO_Initure;

    __HAL_RCC_DCMI_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_Initure.Mode      = GPIO_MODE_AF_PP;
    GPIO_Initure.Pull      = GPIO_PULLUP;
    GPIO_Initure.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_Initure.Alternate = GPIO_AF13_DCMI;

    /* PA4=HSYNC, PA6=PCLK */
    GPIO_Initure.Pin = GPIO_PIN_4 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOA, &GPIO_Initure);

    /* PB6=D5, PB7=VSYNC */
    GPIO_Initure.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &GPIO_Initure);

    /* PC6=D0, PC7=D1, PC8=D2, PC9=D3, PC11=D4 */
    GPIO_Initure.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOC, &GPIO_Initure);

    /* PE5=D6, PE6=D7 */
    GPIO_Initure.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOE, &GPIO_Initure);

    HAL_NVIC_SetPriority(DCMI_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DCMI_IRQn);
}

/**
 * @brief 把摄像头数据/同步引脚重新配置为 DCMI 复用
 * @note  探索者板 SDIO 与 DCMI 共用 PC8/PC9/PC11(PC8=D2/SDIO_D0, PC9=D3/SDIO_D1,
 *        PC11=D4/SDIO_D3)。SD 卡初始化会把这三根脚切成 SDIO，导致 DCMI 丢 3 位数据、
 *        画面发红。所以每次启动采集前都要把这几根脚抢回 DCMI。
 */
void DCMI_PinsConfig(void)
{
    HAL_DCMI_MspInit(&DCMI_Handler);
}

/* 启动采集：先把 LCD 写指针复位，再开 DMA 和 DCMI 捕获 */
void DCMI_Start(void)
{
    DCMI_PinsConfig();          /* 抢回 PC8/9/11(SDIO 可能占用) */

    LCD_SetCursor(0, 0);
    LCD_WriteRAM_Prepare();

    __HAL_DCMI_CLEAR_FLAG(&DCMI_Handler, DCMI_FLAG_FRAMERI);
    __HAL_DCMI_ENABLE_IT(&DCMI_Handler, DCMI_IT_FRAME);   /* Stop 时被关掉，这里恢复 */

    __HAL_DMA_ENABLE(&DMADMCI_Handler);
    DCMI->CR |= DCMI_CR_CAPTURE;
}

/**
 * @brief 配置 JPEG 采集：DMA 把 DCMI 数据搬到 RAM 缓冲区
 * @param buf   JPEG 缓冲区(32 位对齐)
 * @param words 缓冲区字数(每个字 4 字节，可放 2 个像素)
 */
void DCMI_JpegDMA_Init(u32 *buf, u16 words)
{
    cam_mode       = 1;
    jpeg_words     = words;
    jpeg_frame_ready = 0;

    DCMI_DMA_Init((u32)buf, words, DMA_MDATAALIGN_WORD, DMA_MINC_ENABLE);
}

/* 启动一次 JPEG 采集(从当前时刻开始，帧中断时结束) */
void DCMI_JpegStart(void)
{
    DCMI_PinsConfig();          /* 抢回 PC8/9/11(SDIO 可能占用) */

    jpeg_frame_ready = 0;

    __HAL_DMA_SET_COUNTER(&DMADMCI_Handler, jpeg_words);
    __HAL_DCMI_CLEAR_FLAG(&DCMI_Handler, DCMI_FLAG_FRAMERI);
    __HAL_DCMI_ENABLE_IT(&DCMI_Handler, DCMI_IT_FRAME);

    __HAL_DMA_ENABLE(&DMADMCI_Handler);
    DCMI->CR |= DCMI_CR_CAPTURE;
}

/* 停止采集 */
void DCMI_Stop(void)
{
    u32 timeout = 0x00FFFFFFU;

    __HAL_DCMI_DISABLE_IT(&DCMI_Handler, DCMI_IT_FRAME);

    DCMI->CR &= ~DCMI_CR_CAPTURE;
    while ((DCMI->CR & DCMI_CR_CAPTURE) && (--timeout))
    {
        /* 等待当前帧结束；加超时防止摄像头被拔掉时死等 */
    }
    __HAL_DCMI_CLEAR_FLAG(&DCMI_Handler, DCMI_FLAG_FRAMERI);
    __HAL_DMA_DISABLE(&DMADMCI_Handler);
}

/**
 * @brief 设置预览窗口（例如只在屏幕某一块区域显示画面）
 * @param sx,sy    LCD 起始坐标
 * @param width,height 显示区域宽高（需为 4 的倍数）
 */
void DCMI_Set_Window(u16 sx, u16 sy, u16 width, u16 height)
{
    DCMI_Stop();
    LCD_Clear(WHITE);
    LCD_Set_Window(sx, sy, width, height);
    OV2640_OutSize_Set(width, height);

    LCD_SetCursor(0, 0);
    LCD_WriteRAM_Prepare();

    __HAL_DMA_ENABLE(&DMADMCI_Handler);
    DCMI->CR |= DCMI_CR_CAPTURE;
}
