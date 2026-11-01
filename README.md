# Explorer_STM32F407_Demo_0

> 基于 **STM32F407 + OV2640 摄像头 + FSMC LCD + FatFs** 的裸机综合示例：
> 实时预览、拍照、录像，外加一个带图像预览的完整文件管理器。
> 全部 C 语言手写，CMake + `arm-none-eabi-gcc` 构建，不依赖任何 IDE。

![platform](https://img.shields.io/badge/MCU-STM32F407ZGT6-blue)
![lib](https://img.shields.io/badge/HAL-STM32F4xx-green)
![build](https://img.shields.io/badge/build-CMake%20%2B%20arm--none--eabi--gcc-orange)

---

## 工程特性

### 一、相机

| 特性 | 说明 |
| --- | --- |
| 实时预览 | OV2640 → DCMI → DMA → FSMC 显存，RGB565 直写，CPU 不参与搬运 |
| 画面方向 | 原图 / 左右镜像 / 上下翻转 / 旋转 180°，`main.c` 里 `CAMERA_ORIENT` 一行切换 |
| 拍照 | 切 JPEG 模式抓一帧，落盘为 `IMG_xxxx.JPG`（SVGA 800×600） |
| 录像 | 连续抓 JPEG 帧追加写入 `VID_xxxx.MJPG`，本质是 MJPEG 流 |
| 抓帧可靠性 | 不信 DMA 计数，直接在缓冲区扫 `FF D9` 定位帧尾；没有 EOI 自动重采，最多 5 帧 |
| 状态显示 | 上方实时画面，下方叠加帧率、录制帧数、存储容量等信息 |

### 二、文件管理（自研组件）

一个独立可复用的文件管理器（`BSP/Src/filemgr.c`），三种界面状态：

```
① 浏览            ② 二级菜单          ③ 目标目录选择
┌────────────┐    ┌────────────┐     ┌────────────┐
│  图像预览   │    │  图像保持   │     │  图像保持   │
├────────────┤    ├────────────┤     ├────────────┤
│ > IMG_1.JPG│    │ > Copy     │     │ > Copy here│
│   IMG_2.JPG│    │   Move     │     │   Cancel   │
│   [DCIM]   │    │   Delete   │     │   [DCIM]   │
│            │    │   Cancel   │     │   [SUB]    │
└────────────┘    └────────────┘     └────────────┘
```

- **设备层**：SD 卡与板载 SPI Flash 同时挂载，都可用时根目录列出 `[FLASH]` / `[SD CARD]`；只有其中一个时直接进入，不显示多余的一层
- **目录浏览**：目录在前、文件在后，支持长文件名、自动滚动、长按连发
- **图像预览**：上方实时解码显示选中文件（JPEG 软解，支持 1/2/4/8 缩放并居中）
- **复制 / 移动 / 删除**：复制 512 字节分块；移动优先 `f_rename`，跨卷自动退化为「复制 + 删除」；目标已存在则拒绝覆盖
- **交互细节**：
  - 文本永远先刷新，图像后补画——按键响应不被解码阻塞
  - 换文件先清屏再加载；进菜单不打断加载，一直画完并保持
  - 解码过程中按任意键可在**一行之内**中止，按键事件不会被消抖逻辑吞掉

### 三、存储

- 逻辑卷 `0:` = SD 卡（SDIO 4 位模式），逻辑卷 `1:` = 板载 W25Q128 前 12 MB（SPI）
- FatFs R0.12c，`_USE_LFN=1`，支持中文长文件名
- Flash 首次使用无文件系统时自动格式化
- 过滤 FAT 卷标与空名项，列表里不会出现 `[system]` 这类伪条目

### 四、按键

| 按键 | 引脚 | 功能 |
| --- | --- | --- |
| KEY0 | PE4 | 右 / 确认 / 进入 |
| KEY1 | PE3 | 下 |
| KEY2 | PE2 | 左 / 返回 |
| WK_UP | PA0 | 上 |

- 点按响应一次，**按住 400 ms 后每 120 ms 连发**（仅文件列表启用）
- 图像解码期间按键可即时打断

---

## 硬件

| 项目 | 型号 |
| --- | --- |
| 主控 | STM32F407ZGT6（1 MB Flash / 192 KB RAM，工程用 128 KB 主 RAM） |
| 开发板 | 正点原子「探索者」STM32F407 |
| 摄像头 | OV2640（SCCB 配置，DCMI 8 位并口） |
| 显示屏 | FSMC 并口 LCD，480×800 |
| 存储 | MicroSD 卡 + 板载 W25Q128（16 MB，前 12 MB 做 FAT 卷） |

---

## 目录结构

```
├── BSP/
│   ├── Inc/
│   └── Src/
│       ├── lcd.c          FSMC LCD 驱动（含多字号字库）
│       ├── sccb.c         SCCB（类 I2C）时序，用于配置 OV2640
│       ├── ov2640.c       OV2640 驱动 + 初始化寄存器表
│       ├── dcmi.c         DCMI 采集 + DMA，RGB565 / JPEG 两种模式
│       ├── camera.c       拍照 / 录像封装（抓帧、EOI 校验、重采）
│       ├── jpeg_view.c    TJpgDec 移植层：JPEG 显示、MJPEG 播放、可打断解码
│       ├── tjpgd.c        TJpgDec R0.01b 核心（ChaN）
│       ├── filemgr.c      统一文件管理组件
│       ├── storage.c      存储抽象层（多卷、目录、复制 / 移动 / 删除）
│       ├── sdio_sdcard.c  SD 卡驱动（SDIO）
│       ├── spi.c / w25qxx.c   板载 SPI Flash 驱动
│       └── key.c          按键扫描（点按 + 长按连发）
├── Core/                  CubeMX 生成的 HAL 初始化与主程序
├── Drivers/               STM32F4xx HAL + CMSIS
├── Middlewares/FatFS/     FatFs 文件系统
├── cmake/                 CubeMX 生成的构建脚本
└── CMakeLists.txt         用户源文件清单
```

---

## 编译

需要 **CMake ≥ 3.22** 与 **arm-none-eabi-gcc**（推荐直接装 STM32CubeCLT，两者都自带）。

```bash
cmake --preset Release
cmake --build --preset Release
```

产物在 `build/Release/Explorer_STM32F407.elf`。

| 配置 | FLASH | RAM |
| --- | --- | --- |
| Debug | 99.9 KB | 87.9 KB + 2 KB 栈 |
| Release | 73.8 KB | 87.9 KB + 2 KB 栈 |

---

## 烧录

**ST-Link（命令行）**

```bash
STM32_Programmer_CLI -c port=SWD -w build/Release/Explorer_STM32F407.elf -v -rst
```

**ST-Link（图形界面）**：STM32CubeProgrammer 打开 `.elf` / `.hex` / `.bin`（bin 起始地址 `0x08000000`）直接下载。

**串口 ISP**：用 Release 包里的 `.hex`，配合 FlyMcu 等工具。

---

## 上手流程

1. 上电后进入主菜单：`Camera` / `Files`
2. 选 `Camera` → 右方向键进入实时预览，按界面下方提示拍照 / 录像 / 返回
3. 选 `Files` → 右方向键进入文件管理
   - 上下选择文件，上方实时显示画面
   - 右键：目录 → 进入；文件 → 弹出 `Copy / Move / Delete / Cancel`
   - 左键：返回上一层

---

## 几个值得记录的坑

1. **SDIO 与 DCMI 抢引脚**
   SDIO 4 位模式的 PC8 / PC9 / PC11 正好是 DCMI 的 D2 / D3 / D4，两者物理冲突。
   本工程做**引脚时分复用**：启动采集前把三根脚切回 DCMI（`DCMI_PinsConfig()`），访问 SD 前切回 SDIO（`SDIO_PinsConfig()`）。
   只重配 GPIO、不动外设状态，所以录像时「采集 → 写卡」逐帧来回切也没问题。
   *现象*：不处理的话预览整体发红、颜色错乱，而且重新 `DCMI_Init()` 也治不好——因为 `HAL_DCMI_Init` 只在 `State == RESET` 时才会重配 GPIO。

2. **TJpgDec 输出回调返回值**
   正点原子改过的 `tjpgd.c` 写的是 `return (JRESULT)outfunc(...)`，直接把回调返回值当结果码。
   所以回调必须返回 **0**（标准 TJpgDec 是返回 1 表示继续），否则解完第一个 16×16 块就报 `JDR_INTR`。

3. **JPEG 帧尾截断**
   不能用「缓冲区字数 − DMA 剩余计数」算长度，容易偏小导致帧尾丢失。
   改为在缓冲区里扫 `FF D9` 确定真实帧长，扫不到就重采。

4. **FatFs 多卷**
   每个逻辑驱动器必须各自一个 `FATFS` 对象；两个卷共用一个会互相覆盖文件系统参数。

5. **栈要留够**
   `_USE_LFN=1` 时 `DIR` / `FILINFO` 各带 512 字节长文件名缓冲，放栈上会顶爆默认 1 KB 栈。
   本工程把目录对象放静态区，并把 `_Min_Stack_Size` 提到 2 KB。

6. **录像时预览会冻结**
   DCMI 单缓冲，同一时刻只能做「RGB565 直写 LCD」或「JPEG 写 RAM」其中一件，这是硬件限制，不是 bug。

---

## 版本

| 版本 | 内容 |
| --- | --- |
| v1.0.0 | 首个发布版本：相机预览 / 拍照 / 录像 + 统一文件管理组件 |

---

## 许可

`Drivers/`、`Middlewares/FatFS/`、`BSP/Src/tjpgd.c` 分别来自 STMicroelectronics、ChaN 与正点原子，遵循各自原始许可。其余代码可自由使用。
