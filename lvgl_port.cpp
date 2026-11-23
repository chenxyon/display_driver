/**
 * @file     lvgl_port.cpp
 * @brief    LVGL 移植层组件实现 — C++ class（单例）
 *
 * 功能：管理 LVGL 初始化、多显示设备创建与 LVGL 任务
 * 修改：2026-11-21 由 C API（lvgl_port_* 函数）重构为 LvglPort 单例类；
 *       修复原实现 init 与 start 重复创建 LVGL 任务的问题；
 *       刷屏回调经 lv_display user_data 直达驱动实例（原线性查表）
 */
#include "lvgl_port.h"

#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "lvgl_port";

LvglPort &LvglPort::instance()
{
    static LvglPort inst;
    return inst;
}

/* ==================== LVGL 回调 ==================== */

void LvglPort::flushThunk(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap)
{
    /* user_data 直达驱动实例，免查表 */
    DisplayDriver *driver = (DisplayDriver *)lv_display_get_user_data(disp);
    if (driver != nullptr) {
        driver->flush(disp, area, pxMap);
    } else {
        lv_display_flush_ready(disp);
    }
}

void LvglPort::rounderThunk(lv_display_t *disp, lv_area_t *area)
{
    DisplayDriver *driver = (DisplayDriver *)lv_display_get_user_data(disp);
    if (driver != nullptr) {
        driver->rounder(disp, area);
    }
}

void LvglPort::taskThunk(void *arg)
{
    ((LvglPort *)arg)->taskLoop();
    vTaskDelete(NULL);
}

void LvglPort::taskLoop()
{
    ESP_LOGI(TAG, "LVGL 任务已启动");
    while (m_taskRunning) {
        if (m_lockFn != nullptr) {
            m_lockFn();
        }
        lv_timer_handler();
        if (m_unlockFn != nullptr) {
            m_unlockFn();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGI(TAG, "LVGL 任务已退出");
    m_taskHandle = nullptr;
}

/* ==================== 初始化 ==================== */

bool LvglPort::initDisplay(DisplayType type, const DisplayPinConfig &pins)
{
    if (m_displayCount >= MAX_DISPLAYS) {
        ESP_LOGE(TAG, "显示设备数量已达上限 %d", MAX_DISPLAYS);
        return false;
    }

    /* 首次初始化 LVGL 核心 */
    if (!m_lvglInitialized) {
        lv_init();
        lv_tick_set_cb([]() { return (uint32_t)(esp_timer_get_time() / 1000); });
        m_lvglInitialized = true;
    }

    /* 经显示管理器初始化驱动 */
    DisplayManager &mgr = DisplayManager::instance();
    if (!mgr.init(type, pins)) {
        ESP_LOGE(TAG, "显示驱动初始化失败（类型 %d）", (int)type);
        return false;
    }
    DisplayDriver *driver = mgr.current();

    /* 创建 LVGL 显示设备 */
    lv_display_t *display = lv_display_create(driver->getWidth(), driver->getHeight());
    if (display == nullptr) {
        ESP_LOGE(TAG, "lv_display_create 失败");
        return false;
    }

    /* 单色屏用 I1 格式，彩色屏用 RGB565 */
    lv_color_format_t fmt = driver->isMonochrome() ? LV_COLOR_FORMAT_I1 : LV_COLOR_FORMAT_RGB565;
    lv_display_set_color_format(display, fmt);

    /* 手动分配绘制缓冲：
     * - TFT 彩色屏缓冲较大（320x240x2=150KB），优先 PSRAM；
     * - OLED 单色屏缓冲很小（<=1KB+8B 调色板），用内部 RAM 即可。
     * 不用 lv_display_create_draw_buf：其走 LVGL 内部堆，放不下大缓冲。
     *
     * 渲染模式（沿用 v2.2 行为）：
     * - 单色屏 FULL：LVGL 自动渲染全屏，消除 PARTIAL 区域对齐/stride 歧义；
     * - 彩色屏 DIRECT：局部刷新，减少传输量 */
    int32_t stride = lv_draw_buf_width_to_stride(driver->getWidth(), fmt);
    size_t bufSize = (size_t)stride * driver->getHeight();
    if (fmt == LV_COLOR_FORMAT_I1) {
        bufSize += 8; /* I1 格式需在缓冲头部预留 2 项调色板（2 x lv_color32_t） */
    }
    lv_display_render_mode_t renderMode =
        driver->isMonochrome() ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_DIRECT;

    void *buf1 = NULL;
    void *buf2 = NULL;
    if (driver->isMonochrome()) {
        buf1 = malloc(bufSize);
        if (driver->isDoubleBufferEnabled()) {
            buf2 = malloc(bufSize);
        }
    } else {
        buf1 = heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf1 == NULL) {
            buf1 = malloc(bufSize); /* PSRAM 不可用时退回内部 RAM */
        }
        if (driver->isDoubleBufferEnabled()) {
            buf2 = heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (buf2 == NULL) {
                buf2 = malloc(bufSize);
            }
        }
    }
    if (buf1 == NULL) {
        ESP_LOGE(TAG, "分配绘制缓冲失败（%u 字节）", (unsigned)bufSize);
        lv_display_delete(display);
        return false;
    }
    lv_display_set_buffers(display, buf1, buf2, bufSize, renderMode);

    /* 挂接回调；user_data 存驱动实例，刷屏回调直达 */
    lv_display_set_flush_cb(display, flushThunk);
    lv_display_set_rounder_cb(display, rounderThunk);
    lv_display_set_user_data(display, driver);

    /* 记录条目 */
    DisplayEntry &entry = m_displays[m_displayCount];
    entry.display = display;
    entry.driver = driver;
    entry.buf1 = buf1;
    entry.buf2 = buf2;
    entry.type = type;
    entry.initialized = true;
    m_displayCount++;

    if (m_currentDisplay == nullptr) {
        m_currentDisplay = display;
    }

    ESP_LOGI(TAG, "显示设备已创建：%s（%dx%d，%s）", driver->getName(),
             driver->getWidth(), driver->getHeight(),
             driver->isMonochrome() ? "I1" : "RGB565");
    return true;
}

bool LvglPort::init(DisplayType type, const DisplayPinConfig &pins)
{
    return initDisplay(type, pins);
}

bool LvglPort::initMulti(const DisplayType *types, const DisplayPinConfig *pins, int count)
{
    if (types == nullptr || pins == nullptr || count <= 0) {
        return false;
    }
    if (count > MAX_DISPLAYS) {
        ESP_LOGW(TAG, "设备数量 %d 超出上限 %d，截断", count, MAX_DISPLAYS);
        count = MAX_DISPLAYS;
    }
    bool allOk = true;
    for (int i = 0; i < count; i++) {
        if (!initDisplay(types[i], pins[i])) {
            allOk = false;
        }
    }
    return allOk;
}

/* ==================== 任务管理 ==================== */

bool LvglPort::start()
{
    if (m_taskRunning) {
        ESP_LOGW(TAG, "LVGL 任务已在运行");
        return true;
    }
    if (m_displayCount == 0) {
        ESP_LOGE(TAG, "无显示设备，请先 init");
        return false;
    }
    m_taskRunning = true;
    if (xTaskCreatePinnedToCore(taskThunk, "lvgl_task", 8192, this, 5, &m_taskHandle, 1) != pdPASS) {
        m_taskRunning = false;
        m_taskHandle = nullptr;
        ESP_LOGE(TAG, "创建 LVGL 任务失败");
        return false;
    }
    return true;
}

bool LvglPort::stop()
{
    if (!m_taskRunning) {
        return true;
    }
    m_taskRunning = false;
    /* 等待任务自行退出 */
    for (int i = 0; i < 200 && m_taskHandle != nullptr; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return m_taskHandle == nullptr;
}

bool LvglPort::deinit()
{
    stop();

    for (int i = 0; i < m_displayCount; i++) {
        DisplayEntry &entry = m_displays[i];
        if (entry.display != nullptr) {
            lv_display_delete(entry.display);
        }
        /* 缓冲为手动分配，需手动释放 */
        free(entry.buf1);
        free(entry.buf2);
        entry = {};
    }
    m_displayCount = 0;
    m_currentDisplay = nullptr;

    if (m_lvglInitialized) {
        lv_deinit();
        m_lvglInitialized = false;
    }

    ESP_LOGI(TAG, "LVGL 已释放");
    return true;
}

/* ==================== 访问接口 ==================== */

lv_display_t *LvglPort::getDisplayById(int id) const
{
    if (id < 0 || id >= m_displayCount) {
        return nullptr;
    }
    return m_displays[id].display;
}

void LvglPort::setLockFunctions(LockFn lockFn, LockFn unlockFn)
{
    m_lockFn = lockFn;
    m_unlockFn = unlockFn;
    ESP_LOGI(TAG, "调度锁回调已设置");
}

void LvglPort::lock()
{
    if (m_lockFn != nullptr) {
        m_lockFn();
    }
}

void LvglPort::unlock()
{
    if (m_unlockFn != nullptr) {
        m_unlockFn();
    }
}

const char *LvglPort::version()
{
    return lv_version();
}
