# Display Driver 组件 v1.0（C++ class 版）

## 简介

Display Driver 组件提供多屏幕适配的显示功能封装，支持 SSD1306 OLED 与 ILI9341 TFT 屏幕。组件由驱动抽象层（`DisplayDriver` 基类 + `DisplayManager` 注册表）、LVGL 移植层（`LvglPort`）与显示工具（`DisplayUtils`）组成。

## 功能特性

- **多屏幕支持**：SSD1306 OLED（0.91"/0.96"）、ILI9341 TFT（2.4" 横/竖屏）
- **驱动抽象**：`DisplayDriver` 虚基类 + `DisplayManager` 注册表，按类型自动匹配
- **多实例安全**：每个方向/规格一个独立驱动实例，状态为成员变量，支持多屏共存
- **LVGL 9 移植层**：最多 4 屏，单色屏 I1 格式 FULL 渲染、彩色屏 RGB565 DIRECT 渲染，TFT 缓冲优先 PSRAM
- **电源管理**：`sleep()` / `wake()` 休眠唤醒（电池设备省电）
- **背光控制**：开关 + PWM（简化为阈值开关，精细调光可外接 LEDC）
- **FPS 统计**：刷屏次数/耗时/平均耗时 + 1 秒滑动窗口帧率
- **显示工具**：文本类型检测（中/英/图标）、标签创建、富文本、清屏

## 文件结构

```
display_driver/
├── display_driver.h/.cpp   # 枚举/配置结构体 + DisplayDriver 基类 + DisplayManager 单例
├── ili9341_tft.h/.cpp      # ILI9341 TFT 驱动类（横/竖屏两个静态实例）
├── ssd1306_oled.h/.cpp     # SSD1306 OLED 驱动类（0.91/0.96 寸两个静态实例）
├── lvgl_port.h/.cpp        # LVGL 移植层单例（多屏管理 + LVGL 任务）
├── display_utils.h/.cpp    # 显示工具静态类
├── library.json
└── README.md
```

## 快速上手

### 单屏（TFT 横屏）

```cpp
#include "lvgl_port.h"

DisplayPinConfig pins = {
    .sda = GPIO_NUM_11, .scl = GPIO_NUM_12, .reset = GPIO_NUM_13,
    .dc = GPIO_NUM_14, .cs = GPIO_NUM_10, .backlight = GPIO_NUM_9,
};
LvglPort::instance().init(DisplayType::ILI9341_TFT_HOR, pins);
LvglPort::instance().start();   // 启动 LVGL 任务
```

### 多屏（TFT + OLED）

```cpp
DisplayType types[] = { DisplayType::ILI9341_TFT_HOR, DisplayType::SSD1306_OLED_096 };
DisplayPinConfig pins[2] = { tftPins, oledPins };
LvglPort::instance().initMulti(types, pins, 2);
LvglPort::instance().start();

lv_display_t *second = LvglPort::instance().getDisplayById(1);
```

### 直接绘制（不经 LVGL）

```cpp
DisplayDriver *drv = DisplayManager::instance().current();
drv->fill(0, 0, 100, 100, Ili9341Tft::BLUE);
drv->drawPixel(50, 50, Ili9341Tft::RED);
drv->setBacklight(true);
drv->sleep();   // 休眠
drv->wake();    // 唤醒
```

### 显示工具

```cpp
#include "display_utils.h"

DisplayUtils::clearScreen();
DisplayUtils::chineseText("你好", 10, 10, 14);
DisplayUtils::englishText("Hello", 10, 40, 16);
lv_obj_t *lbl = DisplayUtils::createLabelFmt(parent, TextType::ENGLISH, "FPS: %d", 30);
```

## API 概览

### DisplayManager（单例）

| 方法 | 说明 |
|------|------|
| `init(type, pins)` | 按类型查驱动并初始化，设为当前设备 |
| `deinit()` | 反初始化当前设备 |
| `current()` | 当前驱动实例 |
| `width() / height() / isMonochrome()` | 当前设备参数 |
| `setBacklight(on) / setBacklightPwm(duty)` | 背光 |
| `fpsStats() / resetFpsStats()` | FPS 统计 |
| `registerDriver(drv) / findDriver(type) / printRegistry()` | 注册表操作 |

### LvglPort（单例）

| 方法 | 说明 |
|------|------|
| `init(type, pins)` | 初始化单个显示设备（不启动任务） |
| `initMulti(types, pins, count)` | 初始化多个显示设备（最多 4 个） |
| `start() / stop()` | 启动/停止 LVGL 任务（重复调用安全） |
| `deinit()` | 释放全部设备与缓冲 |
| `getDisplay() / getDisplayById(id) / getDisplayCount()` | 设备访问 |
| `setLockFunctions(lock, unlock)` | 挂接调度锁（如与 SystemScheduler 共用一把锁） |
| `lock() / unlock()` | 手动加解锁 |
| `version()` | LVGL 版本字符串 |

### DisplayDriver（基类，子类实现具体设备）

`init / deinit / flush / rounder / setPx / drawPixel / fill / setBacklight /
setBacklightPwm / sleep / wake / getFpsStats / resetFpsStats /
enableDoubleBuffer / isDoubleBufferEnabled`

## 可覆盖默认配置（build_flags）

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `CONFIG_DISPLAY_WIDTH` | 320 | TFT 默认宽度 |
| `CONFIG_DISPLAY_HEIGHT` | 240 | TFT 默认高度 |
| `CONFIG_OLED_WIDTH` | 128 | OLED 默认宽度 |
| `CONFIG_OLED_HEIGHT` | 64 | OLED 默认高度 |
| `CONFIG_OLED_I2C_ADDR` | 0x3C | OLED I2C 地址 |
| `CONFIG_ILI9341_SPI_FREQ_HZ` | 40MHz | ILI9341 SPI 频率 |

## 技术说明

- **单色屏渲染**：I1 格式 + FULL 渲染模式，LVGL 自动渲染全屏，flush 跳过 8 字节调色板、
  按 `lv_draw_buf_width_to_stride` 计算行距，I1 白(0)→SSD1306 亮(1) 取反后
  以水平地址模式一次性连续写入所有页（256 字节分块）
- **彩色屏渲染**：RGB565 + DIRECT 渲染模式，局部刷新；缓冲优先分配在 PSRAM
- **刷屏回调**：`lv_display` 的 user_data 直达驱动实例，免查表
- **初始化序列**：ILI9341 命令表与原 C 版本逐字节一致，勿按 datasheet 臆改

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0.0 | 2026-11-21 | 重构为 C++ class（DisplayDriver/DisplayManager/LvglPort/DisplayUtils/Ili9341Tft/Ssd1306Oled）；修复 init/start 重复创建 LVGL 任务；刷屏日志降级；新增休眠/唤醒电源管理 |
| v2.2 | - | ESP-IDF C 版本：单色屏切 FULL 渲染模式 |

## 许可证

MIT License
