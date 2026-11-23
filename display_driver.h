/**
 * @file     display_driver.h
 * @brief    通用显示驱动组件 — C++ class
 *
 * 功能：显示驱动抽象基类 + 显示管理器（注册表/单例），
 *       支持 TFT（ILI9341 横/竖屏）与 OLED（SSD1306 0.91/0.96 寸）多种显示设备，
 *       统一的初始化、刷屏、背光与 FPS 统计接口。
 * 修改：2026-11-21 由 C API（display_driver_t 结构体 + 函数指针注册表）
 *       重构为 DisplayDriver 抽象基类 + DisplayManager 单例；
 *       Kconfig 配置改为 #ifndef 默认值（可用 build_flags 覆盖）
 */
#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>

#include "driver/gpio.h"
#include "lvgl.h"

/* ==================== 可覆盖默认配置 ==================== */

#ifndef CONFIG_DISPLAY_WIDTH
#define CONFIG_DISPLAY_WIDTH 320 /* TFT 默认宽度 */
#endif

#ifndef CONFIG_DISPLAY_HEIGHT
#define CONFIG_DISPLAY_HEIGHT 240 /* TFT 默认高度 */
#endif

#ifndef CONFIG_OLED_WIDTH
#define CONFIG_OLED_WIDTH 128 /* OLED 默认宽度 */
#endif

#ifndef CONFIG_OLED_HEIGHT
#define CONFIG_OLED_HEIGHT 64 /* OLED 默认高度 */
#endif

#ifndef CONFIG_OLED_I2C_ADDR
#define CONFIG_OLED_I2C_ADDR 0x3C /* OLED I2C 地址 */
#endif

#ifndef CONFIG_ILI9341_SPI_FREQ_HZ
#define CONFIG_ILI9341_SPI_FREQ_HZ (40 * 1000 * 1000) /* ILI9341 SPI 频率 */
#endif

/* ==================== 枚举与配置结构体 ==================== */

/** 显示设备类型 */
enum class DisplayType : uint8_t {
    ILI9341_TFT_HOR  = 0, /* 2.4寸 ILI9341 TFT 横屏（320x240） */
    ILI9341_TFT_VER  = 1, /* 2.4寸 ILI9341 TFT 竖屏（240x320） */
    SSD1306_OLED_091 = 2, /* 0.91寸 SSD1306 OLED（128x32） */
    SSD1306_OLED_096 = 3, /* 0.96寸 SSD1306 OLED（128x64） */
};

/** 文本类型（用于工具函数自动选字体） */
enum class TextType : uint8_t {
    CHINESE = 0, /* 中文 */
    ENGLISH = 1, /* 英文 */
    ICON    = 2, /* 图标 */
};

/** 显示设备引脚配置（I2C 设备用 sda/scl，SPI 设备用全部） */
struct DisplayPinConfig {
    gpio_num_t sda       = GPIO_NUM_NC; /* I2C SDA / SPI MOSI */
    gpio_num_t scl       = GPIO_NUM_NC; /* I2C SCL / SPI SCLK */
    gpio_num_t reset     = GPIO_NUM_NC; /* 复位脚（可选） */
    gpio_num_t dc        = GPIO_NUM_NC; /* SPI 数据/命令选择 */
    gpio_num_t cs        = GPIO_NUM_NC; /* SPI 片选 */
    gpio_num_t backlight = GPIO_NUM_NC; /* 背光脚（可选） */
};

/** 显示设备参数 */
struct DisplayParams {
    int width            = CONFIG_DISPLAY_WIDTH;
    int height           = CONFIG_DISPLAY_HEIGHT;
    int i2c_addr         = CONFIG_OLED_I2C_ADDR; /* I2C 设备地址 */
    int spi_freq         = CONFIG_ILI9341_SPI_FREQ_HZ;
    bool is_monochrome   = false; /* 是否单色屏（OLED 1bpp） */
    bool use_double_buffer = false; /* 是否启用双缓冲 */
};

/** FPS / 刷屏统计 */
struct DisplayFpsStats {
    uint32_t frame_count       = 0; /* 累计帧数 */
    uint32_t flush_count       = 0; /* 累计刷屏次数 */
    uint32_t flush_time_us     = 0; /* 累计刷屏耗时（微秒） */
    uint32_t last_fps          = 0; /* 最近一秒帧率 */
    uint32_t last_flush_time_us = 0; /* 最近一次刷屏耗时 */
    uint32_t avg_flush_time_us = 0; /* 平均刷屏耗时 */
    uint32_t last_frame_time_us = 0; /* 最近一帧耗时 */
};

/* ==================== 显示驱动抽象基类 ==================== */

/**
 * 显示驱动抽象基类
 *
 * 功能：统一各显示设备的初始化、LVGL 刷屏、画点/填充、背光接口，
 *       具体设备（ILI9341 / SSD1306）继承实现
 */
class DisplayDriver {
public:
    virtual ~DisplayDriver() = default;

    /* ---- 设备信息 ---- */
    DisplayType getType() const { return m_type; }
    const char *getName() const { return m_name; }
    int getWidth() const { return m_params.width; }
    int getHeight() const { return m_params.height; }
    bool isMonochrome() const { return m_params.is_monochrome; }

    /* ---- 双缓冲 ---- */
    bool isDoubleBufferEnabled() const { return m_params.use_double_buffer; }
    void enableDoubleBuffer(bool enable) { m_params.use_double_buffer = enable; }

    /* ---- 生命周期 ---- */
    virtual bool init(const DisplayPinConfig &pins) = 0;
    virtual void deinit() = 0;

    /* ---- LVGL 接口 ---- */
    virtual void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) = 0;
    virtual void rounder(lv_display_t *disp, lv_area_t *area)
    {
        (void)disp;
        (void)area;
    }
    virtual void setPx(lv_display_t *disp, uint8_t *buf, int32_t bufW, int32_t x, int32_t y,
                       lv_color_t color, lv_opa_t opa)
    {
        (void)disp;
        (void)buf;
        (void)bufW;
        (void)x;
        (void)y;
        (void)color;
        (void)opa;
    }

    /* ---- 直接绘制（不经 LVGL） ---- */
    virtual int drawPixel(int x, int y, uint16_t color) = 0;
    virtual int fill(int x1, int y1, int x2, int y2, uint16_t color) = 0;

    /* ---- 背光（无背光的设备保持空实现） ---- */
    virtual void setBacklight(bool on) { (void)on; }
    virtual void setBacklightPwm(uint8_t duty) { (void)duty; }

    /* ---- 电源管理（电池设备空闲时可休眠省电） ---- */
    virtual void sleep() {}
    virtual void wake() {}

    /* ---- FPS 统计 ---- */
    const DisplayFpsStats &getFpsStats() const { return m_fps; }
    void resetFpsStats() { m_fps = {}; }

protected:
    DisplayDriver(DisplayType type, const char *name, const DisplayParams &params)
        : m_type(type), m_name(name), m_params(params) {}

    /**
     * @brief  FPS 统计更新（供子类 flush 调用）
     *
     * 功能：累计刷屏次数/耗时，并按 1 秒滑动窗口计算帧率
     */
    void updateFpsStats(uint32_t elapsedUs);

    DisplayType   m_type;
    const char   *m_name;
    DisplayParams m_params;
    DisplayFpsStats m_fps = {};
    uint32_t m_fpsWindowStartMs = 0; /* FPS 窗口起点 */
    uint32_t m_fpsWindowFrames  = 0; /* FPS 窗口内帧数 */
};

/* ==================== 显示管理器 ==================== */

/**
 * 显示管理器（单例）
 *
 * 功能：维护驱动注册表，负责按类型查找、初始化当前显示设备，
 *       并代理背光与 FPS 统计访问。首次使用时自动注册全部内置驱动。
 */
class DisplayManager {
public:
    static constexpr int MAX_DRIVERS = 8; /*!< 注册表容量 */

    /* 全局唯一实例 */
    static DisplayManager &instance();

    /* ---- 注册表 ---- */
    bool registerDriver(DisplayDriver *driver);
    DisplayDriver *findDriver(DisplayType type);
    void printRegistry() const;

    /* ---- 初始化 ---- */
    bool init(DisplayType type, const DisplayPinConfig &pins);
    void deinit();

    /* ---- 当前设备 ---- */
    DisplayDriver *current() { return m_current; }
    int width() const { return m_current ? m_current->getWidth() : 0; }
    int height() const { return m_current ? m_current->getHeight() : 0; }
    bool isMonochrome() const { return m_current ? m_current->isMonochrome() : false; }

    /* ---- 背光 ---- */
    void setBacklight(bool on);
    void setBacklightPwm(uint8_t duty);

    /* ---- FPS 统计 ---- */
    const DisplayFpsStats &fpsStats() const;
    void resetFpsStats();

private:
    DisplayManager() = default;
    void registerBuiltinDrivers();

    DisplayDriver *m_drivers[MAX_DRIVERS] = {};
    int m_driverCount = 0;
    DisplayDriver *m_current = nullptr;
    bool m_builtinRegistered = false;
};

#endif /* DISPLAY_DRIVER_H */
