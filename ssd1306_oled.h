/**
 * @file     ssd1306_oled.h
 * @brief    SSD1306 OLED 显示驱动（0.91寸 128x32 / 0.96寸 128x64）— C++ class
 *
 * 功能：SSD1306 I2C 接口 OLED 显示驱动（1bpp 单色），
 *       支持 0.91 寸与 0.96 寸两种规格。
 * 修改：2026-11-21 由 C API（ssd1306_oled_* 函数 + 共享静态句柄/缓冲）
 *       重构为 Ssd1306Oled 类；每种规格一个独立实例，刷屏暂存缓冲改为实例成员
 */
#ifndef SSD1306_OLED_H
#define SSD1306_OLED_H

#include "display_driver.h"
#include "driver/i2c.h"

/**
 * SSD1306 OLED 驱动类
 *
 * 功能：一个实例对应一块屏幕；内置 0.91/0.96 寸两个静态实例，
 *       由 DisplayManager 注册使用
 */
class Ssd1306Oled : public DisplayDriver {
public:
    /* ---- 单色屏颜色 ---- */
    static constexpr uint16_t WHITE = 0xFFFF;
    static constexpr uint16_t BLACK = 0x0000;

    explicit Ssd1306Oled(DisplayType type);

    /* 内置实例：0.91 寸 / 0.96 寸 */
    static Ssd1306Oled &driver091();
    static Ssd1306Oled &driver096();

    /* ---- DisplayDriver 接口 ---- */
    bool init(const DisplayPinConfig &pins) override;
    void deinit() override;
    void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) override;
    void setPx(lv_display_t *disp, uint8_t *buf, int32_t bufW, int32_t x, int32_t y,
               lv_color_t color, lv_opa_t opa) override;
    int drawPixel(int x, int y, uint16_t color) override;
    int fill(int x1, int y1, int x2, int y2, uint16_t color) override;
    void sleep() override;
    void wake() override;

private:
    /* ---- SSD1306 命令 ---- */
    static constexpr uint8_t CMD_SET_LOW_COLUMN        = 0x00;
    static constexpr uint8_t CMD_SET_HIGH_COLUMN       = 0x10;
    static constexpr uint8_t CMD_SET_MEMORY_MODE       = 0x20;
    static constexpr uint8_t CMD_SET_COLUMN_ADDRESS    = 0x21;
    static constexpr uint8_t CMD_SET_PAGE_ADDRESS      = 0x22;
    static constexpr uint8_t CMD_DEACTIVATE_SCROLL     = 0x2E;
    static constexpr uint8_t CMD_SET_START_LINE        = 0x40;
    static constexpr uint8_t CMD_SET_CONTRAST          = 0x81;
    static constexpr uint8_t CMD_SET_SEGMENT_REMAP     = 0xA1;
    static constexpr uint8_t CMD_DISPLAY_ALL_ON_RESUME = 0xA4;
    static constexpr uint8_t CMD_SET_NORMAL_DISPLAY    = 0xA6;
    static constexpr uint8_t CMD_SET_MULTIPLEX_RATIO   = 0xA8;
    static constexpr uint8_t CMD_DISPLAY_OFF           = 0xAE;
    static constexpr uint8_t CMD_DISPLAY_ON            = 0xAF;
    static constexpr uint8_t CMD_SET_DISPLAY_OFFSET    = 0xD3;
    static constexpr uint8_t CMD_SET_DISPLAY_CLOCK_DIV = 0xD5;
    static constexpr uint8_t CMD_SET_PRECHARGE         = 0xD9;
    static constexpr uint8_t CMD_SET_COM_PINS          = 0xDA;
    static constexpr uint8_t CMD_SET_VCOM_DESELECT     = 0xDB;
    static constexpr uint8_t CMD_CHARGE_PUMP           = 0x8D;
    static constexpr uint8_t CMD_COM_SCAN_DEC          = 0xC8;

    /* ---- I2C 底层 ---- */
    void initI2c(const DisplayPinConfig &pins);
    bool sendCmd(uint8_t cmd);
    bool sendData(const uint8_t *data, size_t len);

    /* ---- 初始化 ---- */
    bool initSequence();

    /* ---- 页式写屏 ---- */
    void setPageWindow(int pageStart, int pageEnd, int colStart, int colEnd);

    i2c_port_t m_i2cPort = I2C_NUM_0;
    int        m_i2cAddr = CONFIG_OLED_I2C_ADDR;
    uint8_t    m_scratch[1024] = {0}; /* flush 页暂存缓冲（128x64 单页 128B × 8 页上限） */
    bool       m_initialized = false;
};

#endif /* SSD1306_OLED_H */
