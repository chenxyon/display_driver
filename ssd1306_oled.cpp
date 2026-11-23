/**
 * @file     ssd1306_oled.cpp
 * @brief    SSD1306 OLED 显示驱动实现（0.91寸 128x32 / 0.96寸 128x64）— C++ class
 *
 * 功能：SSD1306 I2C 接口 OLED 显示驱动（1bpp 单色）
 * 修改：2026-11-21 由 C API（ssd1306_oled_* 函数 + 共享静态句柄/缓冲）
 *       重构为 Ssd1306Oled 类；每种规格一个独立实例，刷屏暂存缓冲改为实例成员
 */
#include "ssd1306_oled.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ssd1306_oled";

/* ==================== 屏幕参数 ==================== */

static const DisplayParams SSD1306_091_PARAMS = {
    .width = 128, .height = 32, .i2c_addr = CONFIG_OLED_I2C_ADDR,
    .spi_freq = 0, .is_monochrome = true, .use_double_buffer = false,
};

static const DisplayParams SSD1306_096_PARAMS = {
    .width = 128, .height = 64, .i2c_addr = CONFIG_OLED_I2C_ADDR,
    .spi_freq = 0, .is_monochrome = true, .use_double_buffer = false,
};

/* ==================== 构造与实例 ==================== */

Ssd1306Oled::Ssd1306Oled(DisplayType type)
    : DisplayDriver(type,
                    type == DisplayType::SSD1306_OLED_091 ? "SSD1306_OLED_091" : "SSD1306_OLED_096",
                    type == DisplayType::SSD1306_OLED_091 ? SSD1306_091_PARAMS : SSD1306_096_PARAMS)
{
}

Ssd1306Oled &Ssd1306Oled::driver091()
{
    static Ssd1306Oled inst(DisplayType::SSD1306_OLED_091);
    return inst;
}

Ssd1306Oled &Ssd1306Oled::driver096()
{
    static Ssd1306Oled inst(DisplayType::SSD1306_OLED_096);
    return inst;
}

/* ==================== I2C 底层 ==================== */

void Ssd1306Oled::initI2c(const DisplayPinConfig &pins)
{
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = pins.sda;
    conf.scl_io_num = pins.scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000;
    i2c_param_config(m_i2cPort, &conf);
    i2c_driver_install(m_i2cPort, conf.mode, 0, 0, 0);
}

bool Ssd1306Oled::sendCmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd}; /* Co=0, D/C#=0：命令 */
    return i2c_master_write_to_device(m_i2cPort, m_i2cAddr, buf, sizeof(buf),
                                      pdMS_TO_TICKS(100)) == ESP_OK;
}

bool Ssd1306Oled::sendData(const uint8_t *data, size_t len)
{
    /* 控制字节 0x40 + 数据，分块发送 */
    uint8_t buf[17];
    buf[0] = 0x40; /* Co=0, D/C#=1：数据 */
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset > 16 ? 16 : len - offset;
        memcpy(buf + 1, data + offset, chunk);
        if (i2c_master_write_to_device(m_i2cPort, m_i2cAddr, buf, chunk + 1,
                                       pdMS_TO_TICKS(100)) != ESP_OK) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

/* ==================== 初始化 ==================== */

bool Ssd1306Oled::initSequence()
{
    bool ok = true;
    ok &= sendCmd(CMD_DISPLAY_OFF);
    ok &= sendCmd(CMD_SET_DISPLAY_CLOCK_DIV);
    ok &= sendCmd(0x80);
    ok &= sendCmd(CMD_SET_MULTIPLEX_RATIO);
    ok &= sendCmd((uint8_t)(m_params.height - 1));
    ok &= sendCmd(CMD_SET_DISPLAY_OFFSET);
    ok &= sendCmd(0x00);
    ok &= sendCmd(CMD_SET_START_LINE | 0x00);
    ok &= sendCmd(CMD_CHARGE_PUMP);
    ok &= sendCmd(0x14);
    ok &= sendCmd(CMD_SET_MEMORY_MODE);
    ok &= sendCmd(0x00); /* 水平寻址模式 */
    ok &= sendCmd(CMD_SET_SEGMENT_REMAP);
    ok &= sendCmd(CMD_COM_SCAN_DEC);
    ok &= sendCmd(CMD_SET_COM_PINS);
    ok &= sendCmd(m_params.height == 64 ? 0x12 : 0x02);
    ok &= sendCmd(CMD_SET_CONTRAST);
    ok &= sendCmd(m_params.height == 64 ? 0xCF : 0x8F);
    ok &= sendCmd(CMD_SET_PRECHARGE);
    ok &= sendCmd(0xF1);
    ok &= sendCmd(CMD_SET_VCOM_DESELECT);
    ok &= sendCmd(0x40);
    ok &= sendCmd(CMD_DISPLAY_ALL_ON_RESUME);
    ok &= sendCmd(CMD_SET_NORMAL_DISPLAY);
    ok &= sendCmd(CMD_DEACTIVATE_SCROLL);
    ok &= sendCmd(CMD_DISPLAY_ON);
    return ok;
}

bool Ssd1306Oled::init(const DisplayPinConfig &pins)
{
    if (m_initialized) {
        ESP_LOGW(TAG, "已初始化，跳过");
        return true;
    }

    m_i2cAddr = m_params.i2c_addr;
    initI2c(pins);

    if (!initSequence()) {
        ESP_LOGE(TAG, "初始化序列发送失败");
        return false;
    }

    /* 清屏 */
    fill(0, 0, m_params.width - 1, m_params.height - 1, BLACK);

    m_initialized = true;
    ESP_LOGI(TAG, "%s 初始化完成（%dx%d，I2C 地址 0x%02X）",
             getName(), m_params.width, m_params.height, m_i2cAddr);
    return true;
}

void Ssd1306Oled::deinit()
{
    if (!m_initialized) {
        return;
    }
    sendCmd(CMD_DISPLAY_OFF);
    i2c_driver_delete(m_i2cPort);
    m_initialized = false;
}

/* ==================== 页式写屏 ==================== */

void Ssd1306Oled::setPageWindow(int pageStart, int pageEnd, int colStart, int colEnd)
{
    sendCmd(CMD_SET_COLUMN_ADDRESS);
    sendCmd((uint8_t)colStart);
    sendCmd((uint8_t)colEnd);
    sendCmd(CMD_SET_PAGE_ADDRESS);
    sendCmd((uint8_t)pageStart);
    sendCmd((uint8_t)pageEnd);
}

/* ==================== DisplayDriver 接口 ==================== */

void Ssd1306Oled::flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap)
{
    int64_t start = esp_timer_get_time();

    int width = area->x2 - area->x1 + 1;
    int height = area->y2 - area->y1 + 1;
    int pageStart = area->y1 / 8;
    int pageEnd = area->y2 / 8;

    ESP_LOGD(TAG, "flush: (%d,%d)-(%d,%d) %dx%d",
             (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2, width, height);

    /* I1 格式：LVGL 在缓冲区前预留 8 字节调色板，需跳过 */
    const uint8_t *pxData = pxMap + 8;

    /* I1 格式 stride：用 LVGL API 计算（含对齐） */
    int stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_I1);

    /* 将 I1 格式转换为 SSD1306 页格式，存入暂存缓冲。
     * 注意：LVGL 9.x I1 默认调色板 索引0=白、索引1=黑，
     *       而 SSD1306 bit=1 亮、bit=0 暗，
     *       故 I1 bit=0（白）-> SSD1306 bit=1（亮），需取反 */
    for (int page = pageStart; page <= pageEnd; page++) {
        uint8_t *pageBuf = m_scratch + (size_t)page * m_params.width + area->x1;

        for (int x = 0; x < width; x++) {
            uint8_t byteVal = 0;
            for (int bit = 0; bit < 8; bit++) {
                int oledY = page * 8 + bit;
                int bufY = oledY - area->y1;
                if (bufY >= 0 && bufY < height) {
                    uint8_t i1Byte = pxData[stride * bufY + (x / 8)];
                    uint8_t i1Bit = 7 - (x % 8);
                    /* I1 bit=0（白）-> SSD1306 亮 */
                    if (!(i1Byte & (1 << i1Bit))) {
                        byteVal |= (1 << bit);
                    }
                }
            }
            pageBuf[x] = byteVal;
        }
    }

    /* 水平地址模式下一次性连续写入所有页数据：
     * SSD1306 自动递增列地址，溢出时自动翻页。
     * 注：OLED 固定 FULL 渲染模式，area 恒为全屏，页数据在暂存缓冲中连续 */
    setPageWindow(pageStart, pageEnd, area->x1, area->x2);

    int totalBytes = (pageEnd - pageStart + 1) * width;
    const uint8_t *dataStart = m_scratch + (size_t)pageStart * m_params.width + area->x1;

    /* 分块写入，每块最多 256 字节，兼容不同 I2C DMA 缓冲大小 */
    const int CHUNK_SIZE = 256;
    int offset = 0;
    while (offset < totalBytes) {
        int chunk = (totalBytes - offset > CHUNK_SIZE) ? CHUNK_SIZE : (totalBytes - offset);
        sendData(dataStart + offset, chunk);
        offset += chunk;
    }

    updateFpsStats((uint32_t)(esp_timer_get_time() - start));
    lv_display_flush_ready(disp);
}

void Ssd1306Oled::setPx(lv_display_t *disp, uint8_t *buf, int32_t bufW, int32_t x, int32_t y,
                        lv_color_t color, lv_opa_t opa)
{
    (void)disp;
    (void)opa;
    /* 旧式页格式 set_px（LVGL9 I1 渲染不走此路径，保留以完备接口） */
    int bytesPerRow = m_params.width;
    uint32_t byteIndex = (y / 8) * bytesPerRow + x;
    uint8_t bitIndex = y % 8;
    if (lv_color_brightness(color) > 128) {
        buf[byteIndex] |= (1 << bitIndex);
    } else {
        buf[byteIndex] &= ~(1 << bitIndex);
    }
    (void)bufW;
}

int Ssd1306Oled::drawPixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= m_params.width || y < 0 || y >= m_params.height) {
        return -1;
    }

    /* 读回当前页数据修改单像素代价过高，这里用 1 字节列窗口写入 */
    uint8_t page = (uint8_t)(y / 8);
    uint8_t bit = (uint8_t)(1 << (y % 8));

    /* SSD1306 无法读显存（未开电荷泵读模式），采用整页缓冲方式：
     * 清屏/填充场景请用 fill；单点绘制仅保证「点亮」语义 */
    setPageWindow(page, page, x, x);
    uint8_t col = (color != BLACK) ? bit : 0x00;
    sendData(&col, 1);
    return 0;
}

int Ssd1306Oled::fill(int x1, int y1, int x2, int y2, uint16_t color)
{
    if (x1 < 0 || y1 < 0 || x2 >= m_params.width || y2 >= m_params.height || x1 > x2 || y1 > y2) {
        return -1;
    }

    int pageStart = y1 / 8;
    int pageEnd = y2 / 8;
    int width = x2 - x1 + 1;
    uint8_t fillByte = (color != BLACK) ? 0xFF : 0x00;

    for (int page = pageStart; page <= pageEnd; page++) {
        /* 组装整页填充数据 */
        uint8_t *pageBuf = m_scratch;
        memset(pageBuf, fillByte, width);
        setPageWindow(page, page, x1, x2);
        sendData(pageBuf, width);
    }
    return 0;
}

void Ssd1306Oled::sleep()
{
    if (!m_initialized) {
        return;
    }
    sendCmd(CMD_DISPLAY_OFF);
    ESP_LOGI(TAG, "已进入休眠");
}

void Ssd1306Oled::wake()
{
    if (!m_initialized) {
        return;
    }
    sendCmd(CMD_DISPLAY_ON);
    ESP_LOGI(TAG, "已唤醒");
}
