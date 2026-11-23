/**
 * @file     ili9341_tft.cpp
 * @brief    ILI9341 TFT 显示驱动实现（2.4寸 320x240）— C++ class
 *
 * 功能：ILI9341 SPI 接口 TFT 显示驱动，支持横屏/竖屏两种方向，
 *       RGB565 彩色显示、背光开关与 PWM 调光
 * 修改：2026-11-21 由 C API（ili9341_tft_* 函数 + 共享静态句柄）重构为
 *       Ili9341Tft 类；每个方向一个独立实例，互不干扰；
 *       刷屏日志由 INFO 降为 DEBUG，避免高频刷屏拖慢帧率
 */
#include "ili9341_tft.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ili9341_tft";

/* ==================== 屏幕参数 ==================== */

static const DisplayParams ILI9341_HOR_PARAMS = {
    .width = 320, .height = 240, .i2c_addr = 0,
    .spi_freq = CONFIG_ILI9341_SPI_FREQ_HZ,
    .is_monochrome = false, .use_double_buffer = false,
};

static const DisplayParams ILI9341_VER_PARAMS = {
    .width = 240, .height = 320, .i2c_addr = 0,
    .spi_freq = CONFIG_ILI9341_SPI_FREQ_HZ,
    .is_monochrome = false, .use_double_buffer = false,
};

/* ==================== 初始化序列 ==================== */

/**
 * 初始化命令表：{命令, 参数个数, 参数...}；参数个数 0x80 表示延时标志（原实现约定）
 * 注意：保持与原 C 版本完全一致的字节序列，勿按 datasheet 臆改
 */
static const uint8_t s_initSeq[] = {
    0x11, 0x80,
    0x3A, 0x01, 0x55,
    0x36, 0x01, 0x60,
    0x21, 0x80,
    0x29, 0x80,
    0xB2, 0x05, 0x0C, 0x0C, 0x00, 0x33, 0x33,
    0xB7, 0x01, 0x35,
    0xBB, 0x01, 0x1F,
    0xC0, 0x01, 0x2C,
    0xC2, 0x01, 0x01,
    0xC3, 0x01, 0x12,
    0xC4, 0x01, 0x20,
    0xC6, 0x01, 0x0F,
    0xD0, 0x02, 0xA4, 0xA1,
    0xE0, 0x0F, 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33, 0x48, 0x17, 0x0F, 0x0F,
    0xE1, 0x0F, 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33, 0x48, 0x17, 0x0F, 0x0F,
};

/* ==================== 构造与实例 ==================== */

Ili9341Tft::Ili9341Tft(DisplayType type)
    : DisplayDriver(type,
                    type == DisplayType::ILI9341_TFT_HOR ? "ILI9341_TFT_HOR" : "ILI9341_TFT_VER",
                    type == DisplayType::ILI9341_TFT_HOR ? ILI9341_HOR_PARAMS : ILI9341_VER_PARAMS)
{
}

Ili9341Tft &Ili9341Tft::hor()
{
    static Ili9341Tft inst(DisplayType::ILI9341_TFT_HOR);
    return inst;
}

Ili9341Tft &Ili9341Tft::vert()
{
    static Ili9341Tft inst(DisplayType::ILI9341_TFT_VER);
    return inst;
}

/* ==================== SPI 底层 ==================== */

void Ili9341Tft::initSpi(const DisplayPinConfig &pins)
{
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = pins.sda;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = pins.scl;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 320 * 240 * 2;

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = m_params.spi_freq;
    devcfg.mode = 0;
    devcfg.spics_io_num = pins.cs;
    devcfg.queue_size = 7;

    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &m_spi));
}

void Ili9341Tft::sendCmd(uint8_t cmd)
{
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(m_spi, &t);
}

void Ili9341Tft::sendData(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(m_spi, &t);
}

void Ili9341Tft::writeCmdData(const uint8_t *cmdData, size_t len)
{
    if (len == 0) {
        return;
    }
    sendCmd(cmdData[0]);
    if (len > 1) {
        sendData(cmdData + 1, len - 1);
    }
}

/* ==================== 初始化 ==================== */

void Ili9341Tft::hardwareReset()
{
    if (m_resetPin == GPIO_NUM_NC) {
        return;
    }
    gpio_set_level(m_resetPin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(m_resetPin, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

void Ili9341Tft::initSequence()
{
    /* 按原实现的解析规则遍历命令表：
     * cmd[1] 为参数个数，最高位（0x80）为延时标志 */
    const uint8_t *p = s_initSeq;
    while (p < s_initSeq + sizeof(s_initSeq)) {
        uint8_t cmd = p[0];
        uint8_t num = p[1];
        bool delay = (num & 0x80) != 0;
        num &= 0x7F;
        writeCmdData(p, num + 1);
        p += num + 2;
        if (delay) {
            vTaskDelay(pdMS_TO_TICKS(120));
        }
        (void)cmd;
    }

    /* 按方向设置扫描方向 */
    uint8_t madctl[2] = {CMD_MADCTL,
                         (uint8_t)(m_type == DisplayType::ILI9341_TFT_HOR ? MADCTL_HOR : MADCTL_VER)};
    writeCmdData(madctl, sizeof(madctl));
}

void Ili9341Tft::initBacklight(gpio_num_t pin)
{
    if (pin == GPIO_NUM_NC) {
        return;
    }
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(pin, 1); /* 默认点亮背光 */
}

bool Ili9341Tft::init(const DisplayPinConfig &pins)
{
    if (m_initialized) {
        ESP_LOGW(TAG, "已初始化，跳过");
        return true;
    }

    m_resetPin = pins.reset;
    m_backlightPin = pins.backlight;

    /* 复位脚 */
    if (m_resetPin != GPIO_NUM_NC) {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << m_resetPin);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
    }

    initSpi(pins);
    hardwareReset();
    initSequence();
    initBacklight(m_backlightPin);

    /* 上电自检：整屏填红，便于目检接线与方向 */
    fill(0, 0, m_params.width - 1, m_params.height - 1, RED);

    m_initialized = true;
    ESP_LOGI(TAG, "%s 初始化完成（%dx%d）", getName(), m_params.width, m_params.height);
    return true;
}

void Ili9341Tft::deinit()
{
    if (!m_initialized) {
        return;
    }
    if (m_spi != nullptr) {
        spi_bus_remove_device(m_spi);
        spi_bus_free(SPI2_HOST);
        m_spi = nullptr;
    }
    m_initialized = false;
}

/* ==================== 绘制底层 ==================== */

void Ili9341Tft::setWindow(int x1, int y1, int x2, int y2)
{
    uint8_t ca[5] = {CMD_CASET,
                     (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF),
                     (uint8_t)(x2 >> 8), (uint8_t)(x2 & 0xFF)};
    uint8_t pa[5] = {CMD_PASET,
                     (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF),
                     (uint8_t)(y2 >> 8), (uint8_t)(y2 & 0xFF)};
    writeCmdData(ca, sizeof(ca));
    writeCmdData(pa, sizeof(pa));
    sendCmd(CMD_RAMWR);
}

void Ili9341Tft::writePixels(const uint16_t *colors, int count)
{
    sendData((const uint8_t *)colors, count * 2);
}

void Ili9341Tft::writeColor(uint16_t color, int count)
{
    /* 静态缓冲分块发送单色数据 */
    static uint16_t buf[512];
    int n = count > 512 ? 512 : count;
    for (int i = 0; i < n; i++) {
        buf[i] = color;
    }
    int remain = count;
    while (remain > 0) {
        int chunk = remain > n ? n : remain;
        sendData((const uint8_t *)buf, chunk * 2);
        remain -= chunk;
    }
}

/* ==================== DisplayDriver 接口 ==================== */

void Ili9341Tft::flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap)
{
    (void)disp;
    int64_t start = esp_timer_get_time();

    int width = area->x2 - area->x1 + 1;
    int height = area->y2 - area->y1 + 1;

    ESP_LOGD(TAG, "flush: (%d,%d)-(%d,%d) %dx%d",
             (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2, width, height);

    setWindow(area->x1, area->y1, area->x2, area->y2);

    /* 分块发送，避免单次传输过大 */
    int chunkSize = width * 2 * 20;
    int total = width * height * 2;
    int offset = 0;
    while (offset < total) {
        int len = (total - offset) > chunkSize ? chunkSize : (total - offset);
        sendData(pxMap + offset, len);
        offset += len;
    }

    /* FPS 统计 */
    updateFpsStats((uint32_t)(esp_timer_get_time() - start));

    lv_display_flush_ready(disp);
}

void Ili9341Tft::rounder(lv_display_t *disp, lv_area_t *area)
{
    (void)disp;
    /* 对齐到 2 像素边界 */
    area->x1 &= ~1;
    area->x2 |= 1;
    area->y1 &= ~1;
    area->y2 |= 1;
}

int Ili9341Tft::drawPixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= m_params.width || y < 0 || y >= m_params.height) {
        return -1;
    }
    setWindow(x, y, x, y);
    writePixels(&color, 1);
    return 0;
}

int Ili9341Tft::fill(int x1, int y1, int x2, int y2, uint16_t color)
{
    if (x1 < 0 || y1 < 0 || x2 >= m_params.width || y2 >= m_params.height || x1 > x2 || y1 > y2) {
        return -1;
    }
    setWindow(x1, y1, x2, y2);
    writeColor(color, (x2 - x1 + 1) * (y2 - y1 + 1));
    return 0;
}

void Ili9341Tft::setBacklight(bool on)
{
    if (m_backlightPin == GPIO_NUM_NC) {
        return;
    }
    gpio_set_level(m_backlightPin, on ? 1 : 0);
    ESP_LOGI(TAG, "背光 %s", on ? "开" : "关");
}

void Ili9341Tft::setBacklightPwm(uint8_t duty)
{
    /* 简化实现：按占空比阈值开关背光；如需精细调光请外接 LEDC */
    setBacklight(duty > 0);
    ESP_LOGD(TAG, "背光 PWM 占空比 %d（简化为开关）", duty);
}

void Ili9341Tft::sleep()
{
    if (!m_initialized) {
        return;
    }
    setBacklight(false);
    sendCmd(CMD_DISPOFF);
    sendCmd(CMD_SLPIN);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "已进入休眠");
}

void Ili9341Tft::wake()
{
    if (!m_initialized) {
        return;
    }
    sendCmd(CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120)); /* 退出睡眠需等待 */
    sendCmd(CMD_DISPON);
    setBacklight(true);
    ESP_LOGI(TAG, "已唤醒");
}
