/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 探索者 STM32F407 摄像头调试终端
  *                   菜单 / 实时预览 / 拍照 / 录像 / SD 卡与板载 Flash 存储
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

#include "lcd.h"
#include "bsp_delay.h"
#include "key.h"
#include "sccb.h"
#include "ov2640.h"
#include "dcmi.h"
#include "camera.h"
#include "storage.h"
#include "jpeg_view.h"
#include "filemgr.h"
/* USER CODE END Includes */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* 调试项目名称：优先使用 CMake 传入的工程名，未定义时用这里的字符串 */
#ifndef PROJECT_TITLE
#define PROJECT_TITLE   "Explorer_STM32F407_LCD"
#endif

#define APP_MENU        0
#define APP_PREVIEW     1

/* 摄像头画面方向：0=原样 1=左右镜像 2=上下翻转 3=旋转180 */
#define CAMERA_ORIENT   1

/* 摄像头界面内的子菜单 */
#define CAM_SUB_MAIN    0       /* 拍照 / 录像 */
#define CAM_SUB_REC     1       /* 开始录制 / 停止 */

/* 文本区各行的纵向偏移(相对文本区起始行) */
#define CAM_FPS_DY      4
#define CAM_ITEM1_DY    24
#define CAM_ITEM2_DY    52
#define CAM_INFO_DY     84
#define CAM_MSG_DY      104

/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static u8  g_app_state = APP_MENU;   /* 当前界面 */
static u8  g_menu_sel  = 0;          /* 主菜单选中项 */
static u8  g_cam_sel   = 0;          /* 摄像头菜单选中项 */
static u8  g_cam_sub   = CAM_SUB_MAIN;
static u8  g_cam_ok    = 0;          /* 摄像头初始化是否成功 */
static u8  g_recording = 0;
static u32 g_rec_frames = 0;
static u32 g_photo_no  = 0;
static u32 g_video_no  = 0;
static char g_msg[44]  = "";         /* 状态提示 */

static u16 g_prev_w;                 /* 预览区宽度(像素) */
static u16 g_prev_h;                 /* 预览区高度(像素) */
static u16 g_stat_y;                 /* 文本区起始行 y */
static u8  g_preview_on;             /* 1: 视频流已启动 */

static char g_fps_buf[32];
static char g_fps_prev[32];
static u8   g_fps_pending;

static u32 g_free_kb = 0, g_total_kb = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */
static void ui_str(u16 x, u16 y, u8 size, const char *s, u32 fc, u32 bc);
static void ui_begin(void);
static void ui_end(void);
static void ui_fps(const char *s);
static void ui_msg(const char *s);
static void menu_draw(void);
static void menu_move(int16_t delta);
static void cam_menu_draw(void);
static void cam_menu_move(int16_t delta);
static void preview_start_stream(void);
static void preview_stop_stream(void);
static void do_photo(void);
static void rec_start(void);
static void rec_stop(void);
static void storage_refresh(void);
static void fmt_size(u32 kb, char *out, u16 n);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ---------------- 基础绘图 ---------------- */

static void ui_str(u16 x, u16 y, u8 size, const char *s, u32 fc, u32 bc)
{
    POINT_COLOR = fc;
    BACK_COLOR  = bc;
    LCD_ShowString(x, y, (u16)(lcddev.width - x), size, size, (const u8 *)s);
}

/**
 * @brief 开始一次界面绘制
 * @note  视频流运行时必须先停 DMA：像素流正在往 LCD 数据口灌数据时发
 *        CASET/RASET/RAMWR 命令，命令参数会被像素字节污染 → 花屏。
 */
static void ui_begin(void)
{
    /* 画文字前一律先停 DCMI 的 DMA 并恢复全屏窗口：
     * 预览时防止像素流污染命令，录像时也能保证文字画在正确位置 */
    __HAL_DMA_DISABLE(&DMADMCI_Handler);
    LCD_Set_Window(0, 0, lcddev.width, lcddev.height);
}

static void ui_end(void)
{
    if (g_preview_on)
    {
        LCD_Set_Window(0, 0, g_prev_w, g_prev_h);
        LCD_SetCursor(0, 0);
        LCD_WriteRAM_Prepare();
        __HAL_DMA_ENABLE(&DMADMCI_Handler);
    }
}

/* 帧率行：逐字符差分刷新，尽量缩短停 DMA 的时间 */
static void ui_fps(const char *s)
{
    u16 i, x = 4;
    const u8 size = 16;

    ui_begin();

    POINT_COLOR = GREEN;
    BACK_COLOR  = BLACK;

    for (i = 0; (s[i] != '\0') && (i < sizeof(g_fps_prev) - 1); i++)
    {
        if (g_fps_prev[i] != s[i])
        {
            LCD_ShowChar(x, (u16)(g_stat_y + CAM_FPS_DY), (u8)s[i], size, 0);
            g_fps_prev[i] = s[i];
        }
        x += size / 2;
    }

    ui_end();
}

/* 状态提示行 */
static void ui_msg(const char *s)
{
    ui_begin();
    ui_str(4, (u16)(g_stat_y + CAM_MSG_DY), 16, s, YELLOW, BLACK);
    ui_end();
}

static void fmt_size(u32 kb, char *out, u16 n)
{
    if (kb >= 1024UL * 1024UL)
    {
        snprintf(out, n, "%lu.%luG", (unsigned long)(kb / 1048576UL),
                 (unsigned long)((kb / 104857UL) % 10UL));
    }
    else if (kb >= 1024UL)
    {
        snprintf(out, n, "%luM", (unsigned long)(kb / 1024UL));
    }
    else
    {
        snprintf(out, n, "%luK", (unsigned long)kb);
    }
}

static void storage_refresh(void)
{
    Storage_GetSpace(&g_free_kb, &g_total_kb);
}

/* ---------------- 主菜单 ---------------- */

static const char *const g_menu_items[] = { "Camera", "Files" };
#define MENU_ITEM_COUNT  (sizeof(g_menu_items) / sizeof(g_menu_items[0]))

#define MENU_ITEM_CAM    0
#define MENU_ITEM_FILES  1

static void menu_draw(void)
{
    char buf[48], s1[12], s2[12];
    u16  i, x, tw;
    u8   size;

    LCD_Set_Window(0, 0, lcddev.width, lcddev.height);
    LCD_Clear(BLACK);

    /* 标题：工程名称，按屏宽自动选字号 */
    tw   = (u16)(strlen(PROJECT_TITLE) * 12);
    size = (tw <= (lcddev.width - 16)) ? 24 : 16;
    tw   = (u16)(strlen(PROJECT_TITLE) * (size / 2));
    x    = (u16)((lcddev.width - tw) / 2);
    ui_str(x, 20, size, PROJECT_TITLE, RED, BLACK);

    LCD_Fill(24, (u16)(20 + size + 12), (u16)(lcddev.width - 25),
             (u16)(20 + size + 13), GRAY);

    /* 硬件状态 */
    snprintf(buf, sizeof(buf), "LCD %04X  %ux%u", lcddev.id, lcddev.width, lcddev.height);
    ui_str(24, (u16)(20 + size + 24), 16, buf, LGRAY, BLACK);

    if (g_cam_ok)
    {
        snprintf(buf, sizeof(buf), "CAM OK  %04X/%04X", ov2640_mid, ov2640_pid);
        ui_str(24, (u16)(20 + size + 44), 16, buf, GREEN, BLACK);
    }
    else
    {
        snprintf(buf, sizeof(buf), "CAM FAIL  %04X", ov2640_mid);
        ui_str(24, (u16)(20 + size + 44), 16, buf, RED, BLACK);
    }

    /* 存储状态 */
    fmt_size(g_free_kb, s1, sizeof(s1));
    fmt_size(g_total_kb, s2, sizeof(s2));
    snprintf(buf, sizeof(buf), "%s free %s/%s  F:%lu", Storage_DevName(), s1, s2,
             (unsigned long)Storage_FileCount());
    ui_str(24, (u16)(20 + size + 64), 16, buf,
           (Storage_Dev() == STORAGE_NONE) ? RED : LGRAY, BLACK);

    /* 菜单项 */
    for (i = 0; i < MENU_ITEM_COUNT; i++)
    {
        u16 y = (u16)(20 + size + 104 + i * 46);

        snprintf(buf, sizeof(buf), "%c %s", (i == g_menu_sel) ? '>' : ' ', g_menu_items[i]);

        if (i == g_menu_sel)
        {
            LCD_Fill(24, (u16)(y - 8), (u16)(lcddev.width - 25), (u16)(y + 30), DARKBLUE);
            ui_str(40, y, 24, buf, WHITE, DARKBLUE);
        }
        else
        {
            ui_str(40, y, 24, buf, LGRAY, BLACK);
        }
    }

    /* 按键提示：一行，贴在屏幕最下方 */
    snprintf(buf, sizeof(buf), "UP/DOWN:Select  RIGHT:OK  LEFT:Back");
    ui_str(8, (u16)(lcddev.height - 18), 16, buf, WHITE, BLACK);
}

static void menu_move(int16_t delta)
{
    int16_t n = (int16_t)g_menu_sel + delta;

    if (n < 0)                       n = (int16_t)MENU_ITEM_COUNT - 1;
    if (n >= (int16_t)MENU_ITEM_COUNT) n = 0;

    if (n != (int16_t)g_menu_sel)
    {
        g_menu_sel = (u8)n;
        menu_draw();
    }
}

/* ---------------- 摄像头界面的文本菜单 ---------------- */

static void cam_menu_draw(void)
{
    char buf[48], s1[12], s2[12];
    u16  y;
    const u8  sel = g_cam_sel;
    const u8  sub = g_cam_sub;

    ui_begin();

    /* 第一项 */
    y = (u16)(g_stat_y + CAM_ITEM1_DY);
    snprintf(buf, sizeof(buf), "%c 1. %s", (sel == 0) ? '>' : ' ',
             (sub == CAM_SUB_MAIN) ? "Photo" : "Start REC");
    ui_str(6, y, 24, buf, (sel == 0) ? WHITE : LGRAY, BLACK);

    /* 第二项 */
    y = (u16)(g_stat_y + CAM_ITEM2_DY);
    snprintf(buf, sizeof(buf), "%c 2. %s", (sel == 1) ? '>' : ' ',
             (sub == CAM_SUB_MAIN) ? "Record" : "Stop REC");
    ui_str(6, y, 24, buf, (sel == 1) ? WHITE : LGRAY, BLACK);

    /* 存储容量 */
    fmt_size(g_free_kb, s1, sizeof(s1));
    fmt_size(g_total_kb, s2, sizeof(s2));
    snprintf(buf, sizeof(buf), "%s free %s/%s  F:%lu", Storage_DevName(), s1, s2,
             (unsigned long)Storage_FileCount());
    ui_str(4, (u16)(g_stat_y + CAM_INFO_DY), 16, buf,
           (Storage_Dev() == STORAGE_NONE) ? RED : LGRAY, BLACK);

    /* 状态提示 */
    ui_str(4, (u16)(g_stat_y + CAM_MSG_DY), 16, g_msg, YELLOW, BLACK);

    ui_end();
}

static void cam_menu_move(int16_t delta)
{
    int16_t n = (int16_t)g_cam_sel + delta;

    if (n < 0)   n = 1;
    if (n > 1)   n = 0;

    g_cam_sel = (u8)n;
    cam_menu_draw();
}

/* ---------------- 预览流控制 ---------------- */

static void preview_start_stream(void)
{
    if (!g_cam_ok)
    {
        return;
    }

    /* 关键：每次启动预览都强制回到 RGB565 + 复位 DCMI。
     * 拍照/录像会临时把传感器切到 YUV422+JPEG、把 DCMI 数据通路指向 RAM，
     * 如果回来时只改输出尺寸而不复位，残留的 JPEG/DSP 状态会让画面整体发红。
     * 参考例程每次在 RGB565 / JPEG 之间切换都会重新 DCMI_Init()，这里对齐。 */
    OV2640_RGB565_Mode();
    DCMI_Init();

    LCD_Set_Window(0, 0, g_prev_w, g_prev_h);
    DCMI_DMA_Init((u32)&LCD->LCD_RAM, 1, DMA_MDATAALIGN_HALFWORD, DMA_MINC_DISABLE);
    OV2640_OutSize_Set(g_prev_w, g_prev_h);
    DCMI_Start();

    g_preview_on = 1;
}

static void preview_stop_stream(void)
{
    if (g_preview_on)
    {
        DCMI_Stop();
        g_preview_on = 0;
    }
    LCD_Set_Window(0, 0, lcddev.width, lcddev.height);
    dcmi_frame_edge = 0;
    g_fps_pending   = 0;
}

/* ---------------- 拍照 ---------------- */

static void do_photo(void)
{
    const u8 *buf = 0;
    u32 len, i;
    char name[16];

    if (!g_cam_ok)
    {
        return;
    }

    if (!g_cam_ok)
    {
        return;
    }

    if (Storage_Dev() == STORAGE_NONE)
    {
        snprintf(g_msg, sizeof(g_msg), "No storage!");
        ui_msg(g_msg);
        return;
    }

    ui_msg("Taking photo...");
    preview_stop_stream();

    if (Camera_JpegBegin(g_prev_w, g_prev_h) != 0)
    {
        ui_msg("Camera error");
        preview_start_stream();
        return;
    }

    len = Camera_JpegGrab(&buf, 3000);

    if (len > 0)
    {
        /* 跳过可能出现在帧头前的无效字节，从 JPEG 起始标记 0xFFD8 开始保存 */
        for (i = 0; (i + 1) < len; i++)
        {
            if (buf[i] == 0xFF && buf[i + 1] == 0xD8)
            {
                break;
            }
        }
        if ((i + 1) >= len)
        {
            i = 0;
        }

        snprintf(name, sizeof(name), "IMG_%04lu.JPG", (unsigned long)(++g_photo_no));

        if (Storage_SaveFile(name, buf + i, len - i) == 0)
        {
            snprintf(g_msg, sizeof(g_msg), "Saved %s %luK", name,
                     (unsigned long)((len - i + 1023) / 1024));
        }
        else
        {
            snprintf(g_msg, sizeof(g_msg), "Save failed");
        }
    }
    else
    {
        snprintf(g_msg, sizeof(g_msg), "Capture timeout");
    }

    Camera_JpegEnd(g_prev_w, g_prev_h);
    storage_refresh();

    preview_start_stream();
    cam_menu_draw();
}

/* ---------------- 录像 ---------------- */

static void rec_start(void)
{
    char name[16];

    if (!g_cam_ok)
    {
        return;
    }

    if (!g_cam_ok)
    {
        return;
    }

    if (Storage_Dev() == STORAGE_NONE)
    {
        snprintf(g_msg, sizeof(g_msg), "No storage!");
        ui_msg(g_msg);
        return;
    }

    snprintf(name, sizeof(name), "VID_%04lu.MJPG", (unsigned long)(g_video_no + 1));

    preview_stop_stream();

    if (Camera_JpegBegin(g_prev_w, g_prev_h) != 0)
    {
        ui_msg("Camera error");
        preview_start_stream();
        return;
    }

    if (Storage_RecOpen(name) != 0)
    {
        ui_msg("Open file failed");
        Camera_JpegEnd(g_prev_w, g_prev_h);
        preview_start_stream();
        return;
    }

    g_video_no++;
    g_rec_frames = 0;
    g_recording  = 1;

    snprintf(g_msg, sizeof(g_msg), "REC %s  0f", name);
    cam_menu_draw();
}

static void rec_stop(void)
{
    Storage_RecClose();
    g_recording = 0;

    Camera_JpegEnd(g_prev_w, g_prev_h);
    storage_refresh();

    snprintf(g_msg, sizeof(g_msg), "Saved %lu frames", (unsigned long)g_rec_frames);

    preview_start_stream();
    cam_menu_draw();
}

/* ---------------- 界面切换 ---------------- */

static void app_enter_preview(void)
{
    LCD_Set_Window(0, 0, lcddev.width, lcddev.height);
    LCD_Clear(BLACK);

    /* 预览区先铺白底，等视频到来 */
    LCD_Set_Window(0, 0, g_prev_w, g_prev_h);
    LCD_Fill(0, 0, (u16)(g_prev_w - 1), (u16)(g_prev_h - 1), WHITE);
    LCD_Set_Window(0, 0, lcddev.width, lcddev.height);

    /* 分隔线 + 黑底文本区 */
    LCD_Fill(0, g_prev_h, (u16)(lcddev.width - 1), (u16)(g_prev_h + 1), GRAY);
    LCD_Fill(0, g_stat_y, (u16)(lcddev.width - 1), (u16)(lcddev.height - 1), BLACK);

    g_cam_sel = 0;
    g_cam_sub = CAM_SUB_MAIN;

    memset(g_fps_prev, 0, sizeof(g_fps_prev));
    ui_fps("FPS: --.- F:-----");

    if (!g_cam_ok)
    {
        snprintf(g_msg, sizeof(g_msg), "CAM not ready");
        cam_menu_draw();
        return;
    }

    storage_refresh();
    snprintf(g_msg, sizeof(g_msg), "READY");
    cam_menu_draw();

    preview_start_stream();
}

static void app_leave_preview(void)
{
    if (g_recording)
    {
        rec_stop();
    }

    preview_stop_stream();

    LCD_Set_Window(0, 0, lcddev.width, lcddev.height);
    storage_refresh();
    menu_draw();
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  key_dir_t key;
  u32  t_last, f_last, now, df, fps10;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  bsp_delay_init();   /* DWT 延时，需在系统时钟切换完成后调用 */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();

  /* USER CODE BEGIN 2 */
  KEY_Init();                 /* 上=WK_UP 左=KEY2 下=KEY1 右=KEY0 */
  LCD_Init();
  LCD_Display_Dir(0);         /* 竖屏 */
  LCD_Clear(BLACK);

  /* 界面布局：预览区 4:3 占顶部，下面是黑底文本区 */
  g_prev_w = lcddev.width;
  g_prev_h = (u16)((lcddev.width * 3U / 4U) & 0xFFFCU);
  g_stat_y = g_prev_h + 2;

  /* 摄像头：恢复为镜像前的初始化，只额外做左右镜像，其余不动 */
  g_cam_ok = (OV2640_Init() == 0) ? 1 : 0;
  if (g_cam_ok)
  {
    OV2640_RGB565_Mode();
    OV2640_SetOrientation(CAMERA_ORIENT);  /* 仅左右镜像 */
    DCMI_Init();
  }

  /* 存储：先 SD 卡，没有则用板载 Flash（首次会自动格式化，需要几秒） */
  ui_str(8, 20, 16, "Storage init...", WHITE, BLACK);
  Storage_Init();
  storage_refresh();

  menu_draw();
  g_app_state = APP_MENU;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  t_last = HAL_GetTick();
  f_last = 0;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* ---- 录像中：不断取帧写文件，任意方向键停止 ---- */
    if (g_recording)
    {
      const u8 *buf = 0;
      u32 len = Camera_JpegGrab(&buf, 400);

      if (len > 0)
      {
        if (Storage_RecWrite(buf, len) == 0)
        {
          g_rec_frames++;
        }

        if ((g_rec_frames % 10U) == 0U)
        {
          snprintf(g_msg, sizeof(g_msg), "REC %lu frames", (unsigned long)g_rec_frames);
          ui_msg(g_msg);
        }
      }

      if (KEY_GetDir() != KEY_DIR_NONE)
      {
        rec_stop();
      }
      continue;
    }

    /* ---- 帧率文本只在帧边界刷新 ---- */
    if (dcmi_frame_edge)
    {
      dcmi_frame_edge = 0;

      if (g_fps_pending && g_preview_on)
      {
        g_fps_pending = 0;
        ui_fps(g_fps_buf);
      }
    }

    /* 解码被打断时捕获到的按键优先处理，避免被 KEY_GetDir 消抖二次检查吞掉 */
    if (Jpeg_KeyEvent != 0)
    {
      key = (key_dir_t)Jpeg_KeyEvent;
      Jpeg_KeyEvent = 0;
    }
    else
    {
      key = KEY_GetDir();
    }

    if (g_app_state == APP_MENU)
    {
      if (key == KEY_DIR_UP)
      {
        menu_move(-1);
      }
      else if (key == KEY_DIR_DOWN)
      {
        menu_move(1);
      }
      else if (key == KEY_DIR_RIGHT)          /* 右：确认 */
      {
        if (g_menu_sel == MENU_ITEM_CAM)
        {
          app_enter_preview();
          g_app_state = APP_PREVIEW;
          t_last = HAL_GetTick();
          f_last = dcmi_frame_count;
        }
        else if (g_menu_sel == MENU_ITEM_FILES)
        {
          /* 统一文件管理组件（阻塞式），返回后回主菜单 */
          FileMgr_Run();
          storage_refresh();
          menu_draw();
          g_app_state = APP_MENU;
        }
      }
    }
    else /* APP_PREVIEW */
    {
      if (key == KEY_DIR_LEFT)                /* 左：返回上一级 */
      {
        if (g_cam_sub == CAM_SUB_REC)
        {
          g_cam_sub = CAM_SUB_MAIN;
          g_cam_sel = 1;
          cam_menu_draw();
        }
        else
        {
          app_leave_preview();
          g_app_state = APP_MENU;
        }
      }
      else if (key == KEY_DIR_UP)
      {
        cam_menu_move(-1);
      }
      else if (key == KEY_DIR_DOWN)
      {
        cam_menu_move(1);
      }
      else if (key == KEY_DIR_RIGHT)          /* 右：确认 */
      {
        if (g_cam_sub == CAM_SUB_MAIN)
        {
          if (g_cam_sel == 0)
          {
            do_photo();
          }
          else
          {
            g_cam_sub = CAM_SUB_REC;
            g_cam_sel = 0;
            cam_menu_draw();
          }
        }
        else /* CAM_SUB_REC */
        {
          if (g_cam_sel == 0)
          {
            rec_start();
          }
          else
          {
            snprintf(g_msg, sizeof(g_msg), "Not recording");
            cam_menu_draw();
          }
        }
      }

      /* 帧率统计 */
      if (g_preview_on)
      {
        now = HAL_GetTick();
        if ((now - t_last) >= 500U)
        {
          df    = dcmi_frame_count - f_last;
          fps10 = (u32)((df * 10000U) / (now - t_last));

          snprintf(g_fps_buf, sizeof(g_fps_buf), "FPS:%2lu.%lu F:%06lu",
                   (unsigned long)((fps10 / 10) % 100UL), (unsigned long)(fps10 % 10),
                   (unsigned long)(dcmi_frame_count % 1000000UL));
          g_fps_pending = 1;

          f_last = dcmi_frame_count;
          t_last = now;
        }
      }
    }
  }
  /* USER CODE END 3 */
}
/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** 板载 8MHz 外部晶振(HSE) -> PLL -> 168MHz 系统时钟
   *  VCO = 8MHz / PLLM(8) * PLLN(336) = 336MHz
   *  SYSCLK = 336 / PLLP(2) = 168MHz
   *  USB/SDIO = 336 / PLLQ(7) = 48MHz
   *  FSMC 时序参数按 168MHz HCLK 标定，此频率不能随意更改
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
