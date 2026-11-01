# Explorer STM32F407 — 摄像头 / 拍照 / 录像 / 文件管理

基于 **STM32F407 + OV2640 + FSMC LCD + FatFs** 的裸机综合示例，全部代码用 C 编写，CMake + `arm-none-eabi-gcc` 构建。

板卡：正点原子「探索者」STM32F407 开发板，LCD 480×800（横竖屏可切换，本工程按 480 宽使用）。

---

## 功能

### 1. 相机（Camera）

- OV2640 实时预览：RGB565 直写 LCD（DCMI → DMA → FSMC 显存），SVGA 800×600 降采样显示
- 画面**左右镜像**（`main.c` 中 `CAMERA_ORIENT` 可切换 4 种方向）
- **拍照**：切 JPEG 模式抓一帧，落盘为 `IMG_xxxx.JPG`
- **录像**：连续抓 JPEG 帧，按 MJPEG（连续 JPEG）格式追加写入 `VID_xxxx.MJPG`

### 2. 文件管理（Files）

一个独立的文件管理组件（`BSP/Src/filemgr.c`），三态界面：

| 状态 | 内容 |
| --- | --- |
| 浏览 | 上方图像预览（JPEG 软解）+ 下方文件列表，高亮框选择 |
| 二级菜单 | `Copy` / `Move` / `Delete` / `Cancel` |
| 目标选择 | 列表顶部固定两条 `Copy here` / `Cancel`，下面选目标目录 |

- **设备层**：SD 卡与板载 SPI Flash 同时挂载，都可用时根目录列出 `[FLASH]` / `[SD CARD]`；只有其中一个时直接进入该设备
- 目录进出、复制（512 字节分块）、移动（同卷 `f_rename`，跨卷复制+删除）、删除
- 目标已存在时拒绝覆盖
- 预览图解码可被按键随时打断：**文本先刷新，图像后补画**；换文件先清屏再加载，进菜单不打断加载，直到画完并保持

### 3. 存储

- 逻辑卷 0 = SD 卡（SDIO 4 位），逻辑卷 1 = 板载 W25Q128 前 12MB（SPI）
- FatFs R0.12c，`_USE_LFN=1`，中文长文件名可用
- Flash 首次使用无文件系统时自动格式化

---

## 目录结构

```
├── BSP/
│   ├── Inc/            板级驱动头文件
│   └── Src/
│       ├── lcd.c       FSMC LCD 驱动（含字库）
│       ├── sccb.c      SCCB（类 I2C）时序，用于配置 OV2640
│       ├── ov2640.c    OV2640 传感器驱动 + 初始化寄存器表
│       ├── dcmi.c      DCMI 采集 + DMA，RGB565/JPEG 两种模式
│       ├── camera.c    拍照 / 录像封装（抓帧、EOI 校验、重采）
│       ├── jpeg_view.c TJpgDec 移植层：JPEG 显示、MJPEG 播放、可打断解码
│       ├── tjpgd.c     TJpgDec R0.01b 核心（ChaN）
│       ├── filemgr.c   统一文件管理组件
│       ├── storage.c   存储抽象层（多卷、目录、复制/移动/删除）
│       ├── sdio_sdcard.c / spi.c / w25qxx.c   SD 卡与 SPI Flash 驱动
│       └── key.c       按键扫描（点按 + 长按连发）
├── Core/               CubeMX 生成的 HAL 初始化与主程序
├── Drivers/            STM32F4xx HAL + CMSIS
├── Middlewares/FatFS/  FatFs 文件系统
└── cmake/              CubeMX 生成的构建脚本
```

---

## 按键

| 按键 | 引脚 | 功能 |
| --- | --- | --- |
| KEY0 | PE4 | 右 / 确认 |
| KEY1 | PE3 | 下 |
| KEY2 | PE2 | 左 / 返回 |
| WK_UP | PA0 | 上 |

- 文件列表中支持**长按连发**（按住 400ms 后每 120ms 一次）
- 图像解码过程中按任意键可立即打断

---

## 编译

需要 CMake ≥ 3.22 与 `arm-none-eabi-gcc`（推荐直接用 STM32CubeCLT，自带两者）。

```bash
cmake --preset Debug
cmake --build --preset Debug
```

产物：`build/Debug/Explorer_STM32F407.elf`，可直接用 ST-Link 下载：

```bash
STM32_Programmer_CLI -c port=SWD -w build/Debug/Explorer_STM32F407.elf -v -rst
```

当前占用：FLASH 约 99.7 KB / 1 MB，RAM 约 87.9 KB + 2 KB 栈 / 128 KB。

---

## 几个值得记录的坑

1. **SDIO 与 DCMI 抢引脚**：SDIO 4 位模式的 PC8/PC9/PC11 正好是 DCMI 的 D2/D3/D4。两者不能同时用，本工程做了**引脚时分复用**——启动采集前把这三根脚切回 DCMI（`DCMI_PinsConfig()`），访问 SD 前切回 SDIO（`SDIO_PinsConfig()`）。
2. **TJpgDec 输出回调返回值**：ATK 版 `tjpgd.c` 里写的是 `return (JRESULT)outfunc(...)`，直接把回调返回值当结果码用，所以回调必须返回 **0**（标准 TJpgDec 是返回 1 表示继续），否则解完第一个 MCU 就报 `JDR_INTR`。
3. **JPEG 帧尾截断**：不能信 `DMA 剩余计数` 算出来的长度，改为在缓冲区里扫 `FF D9` 确定真实帧长，尾部没有 EOI 就重采。
4. **FatFs 多卷**：每个逻辑驱动器必须各自一个 `FATFS` 对象，共用一个会互相覆盖。
5. **录像时预览会冻结**：DCMI 单缓冲，同一时刻只能做「RGB565 直写 LCD」或「JPEG 写 RAM」其中一件，这是硬件限制。

---

## 许可

`Drivers/`、`Middlewares/FatFS/`、`BSP/Src/tjpgd.c` 分别来自 STMicroelectronics、ChaN 与正点原子，遵循各自原始许可。
