/**
 * @file     ili9341_tft.h
 * @brief    ILI9341 TFT 显示驱动（2.4寸 320x240）— C++ class
 *
 * 功能：ILI9341 SPI 接口 TFT 显示驱动，支持横屏/竖屏两种方向，
 *       RGB565 彩色显示、背光开关与 PWM 调光。
 * 修改：2026-11-21 由 C API（ili9341_tft_* 函数 + 共享静态句柄）重构为
 *       Ili9341Tft 类；每个方向一个独立实例，互不干扰
 */
#ifndef ILI9341_TFT_H
#define ILI9341_TFT_H

#include "display_driver.h"
#include "driver/spi_master.h"

/**
 * ILI9341 TFT 驱动类
 *
 * 功能：一个实例对应一个方向的屏幕；内置横屏/竖屏两个静态实例，
 *       由 DisplayManager 注册使用
 */
class Ili9341Tft : public DisplayDriver {
public:
    /* ---- RGB565 常用颜色 ---- */
    static constexpr uint16_t BLACK   = 0x0000;
    static constexpr uint16_t WHITE   = 0xFFFF;
    static constexpr uint16_t RED     = 0xF800;
    static constexpr uint16_t GREEN   = 0x07E0;
    static constexpr uint16_t BLUE    = 0x001F;
    static constexpr uint16_t YELLOW  = 0xFFE0;
    static constexpr uint16_t CYAN    = 0x07FF;
    static constexpr uint16_t MAGENTA = 0xF81F;

    explicit Ili9341Tft(DisplayType type);

    /* 内置实例：横屏 / 竖屏 */
    static Ili9341Tft &hor();
    static Ili9341Tft &vert();

    /* ---- DisplayDriver 接口 ---- */
    bool init(const DisplayPinConfig &pins) override;
    void deinit() override;
    void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) override;
    void rounder(lv_display_t *disp, lv_area_t *area) override;
    int drawPixel(int x, int y, uint16_t color) override;
    int fill(int x1, int y1, int x2, int y2, uint16_t color) override;
    void setBacklight(bool on) override;
    void setBacklightPwm(uint8_t duty) override;
    void sleep() override;
    void wake() override;

private:
    /* ---- 常用命令（完整初始化序列见 ili9341_tft.cpp 的 s_initSeq） ---- */
    static constexpr uint8_t CMD_SLPIN  = 0x10; /* 进入睡眠 */
    static constexpr uint8_t CMD_SLPOUT = 0x11; /* 退出睡眠 */
    static constexpr uint8_t CMD_DISPOFF = 0x28; /* 关显示 */
    static constexpr uint8_t CMD_INVON  = 0x21; /* 反色显示 */
    static constexpr uint8_t CMD_DISPON = 0x29; /* 开显示 */
    static constexpr uint8_t CMD_CASET  = 0x2A; /* 列地址设置 */
    static constexpr uint8_t CMD_PASET  = 0x2B; /* 页地址设置 */
    static constexpr uint8_t CMD_RAMWR  = 0x2C; /* 写显存 */
    static constexpr uint8_t CMD_MADCTL = 0x36; /* 扫描方向/色序 */
    static constexpr uint8_t CMD_PIXFMT = 0x3A; /* 像素格式 */

    /* ---- MADCTL 方向参数 ---- */
    static constexpr uint8_t MADCTL_HOR = 0x60; /* MV | MX | BGR */
    static constexpr uint8_t MADCTL_VER = 0x20; /* MV | BGR */

    /* ---- SPI 底层 ---- */
    void initSpi(const DisplayPinConfig &pins);
    void sendCmd(uint8_t cmd);
    void sendData(const uint8_t *data, size_t len);
    void writeCmdData(const uint8_t *cmdData, size_t len);

    /* ---- 初始化子流程 ---- */
    void hardwareReset();
    void initSequence();
    void initBacklight(gpio_num_t pin);

    /* ---- 绘制底层 ---- */
    void setWindow(int x1, int y1, int x2, int y2);
    void writePixels(const uint16_t *colors, int count);
    void writeColor(uint16_t color, int count);

    spi_device_handle_t m_spi         = nullptr;
    gpio_num_t          m_resetPin    = GPIO_NUM_NC;
    gpio_num_t          m_backlightPin = GPIO_NUM_NC;
    bool                m_initialized = false;
};

#endif /* ILI9341_TFT_H */
