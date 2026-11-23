/**
 * @file     display_driver.cpp
 * @brief    显示管理器实现 — C++ class
 *
 * 功能：驱动注册表维护、按类型查找与初始化当前显示设备、背光与 FPS 代理
 * 修改：2026-11-21 由 display_registry.c 重构为 DisplayManager 单例
 */
#include "display_driver.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "ili9341_tft.h"
#include "ssd1306_oled.h"

static const char *TAG = "display_manager";

/* ==================== DisplayDriver 基类 ==================== */

void DisplayDriver::updateFpsStats(uint32_t elapsedUs)
{
    m_fps.flush_count++;
    m_fps.flush_time_us += elapsedUs;
    m_fps.last_flush_time_us = elapsedUs;
    m_fps.avg_flush_time_us = m_fps.flush_time_us / m_fps.flush_count;
    m_fps.frame_count++;
    m_fpsWindowFrames++;

    /* 1 秒滑动窗口帧率 */
    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
    if (m_fpsWindowStartMs == 0) {
        m_fpsWindowStartMs = nowMs;
    }
    uint32_t windowMs = nowMs - m_fpsWindowStartMs;
    if (windowMs >= 1000) {
        m_fps.last_fps = (uint32_t)((uint64_t)m_fpsWindowFrames * 1000 / windowMs);
        m_fpsWindowStartMs = nowMs;
        m_fpsWindowFrames = 0;
    }
}

/* ==================== DisplayManager ==================== */

DisplayManager &DisplayManager::instance()
{
    static DisplayManager inst;
    return inst;
}

void DisplayManager::registerBuiltinDrivers()
{
    if (m_builtinRegistered) {
        return;
    }
    registerDriver(&Ili9341Tft::hor());
    registerDriver(&Ili9341Tft::vert());
    registerDriver(&Ssd1306Oled::driver091());
    registerDriver(&Ssd1306Oled::driver096());
    m_builtinRegistered = true;
}

bool DisplayManager::registerDriver(DisplayDriver *driver)
{
    if (driver == nullptr) {
        ESP_LOGE(TAG, "驱动为空");
        return false;
    }
    if (m_driverCount >= MAX_DRIVERS) {
        ESP_LOGE(TAG, "驱动注册表已满（%d）", MAX_DRIVERS);
        return false;
    }
    /* 同类型去重 */
    if (findDriver(driver->getType()) != nullptr) {
        ESP_LOGW(TAG, "类型 %d 已注册，跳过", (int)driver->getType());
        return false;
    }
    m_drivers[m_driverCount++] = driver;
    ESP_LOGD(TAG, "注册驱动：%s（类型 %d）", driver->getName(), (int)driver->getType());
    return true;
}

DisplayDriver *DisplayManager::findDriver(DisplayType type)
{
    if (!m_builtinRegistered) {
        registerBuiltinDrivers();
    }
    for (int i = 0; i < m_driverCount; i++) {
        if (m_drivers[i]->getType() == type) {
            return m_drivers[i];
        }
    }
    return nullptr;
}

void DisplayManager::printRegistry() const
{
    ESP_LOGI(TAG, "显示驱动注册表：");
    ESP_LOGI(TAG, "  共 %d 个驱动", m_driverCount);
    for (int i = 0; i < m_driverCount; i++) {
        const DisplayDriver *d = m_drivers[i];
        ESP_LOGI(TAG, "  [%d] %-20s 类型=%d 分辨率=%dx%d %s",
                 i, d->getName(), (int)d->getType(),
                 d->getWidth(), d->getHeight(),
                 d->isMonochrome() ? "单色" : "彩色");
    }
}

bool DisplayManager::init(DisplayType type, const DisplayPinConfig &pins)
{
    DisplayDriver *driver = findDriver(type);
    if (driver == nullptr) {
        ESP_LOGE(TAG, "未找到类型 %d 的驱动", (int)type);
        return false;
    }
    if (!driver->init(pins)) {
        ESP_LOGE(TAG, "驱动 %s 初始化失败", driver->getName());
        return false;
    }
    m_current = driver;
    ESP_LOGI(TAG, "当前显示设备：%s（%dx%d）", driver->getName(),
             driver->getWidth(), driver->getHeight());
    return true;
}

void DisplayManager::deinit()
{
    if (m_current != nullptr) {
        m_current->deinit();
        m_current = nullptr;
    }
}

void DisplayManager::setBacklight(bool on)
{
    if (m_current != nullptr) {
        m_current->setBacklight(on);
    }
}

void DisplayManager::setBacklightPwm(uint8_t duty)
{
    if (m_current != nullptr) {
        m_current->setBacklightPwm(duty);
    }
}

const DisplayFpsStats &DisplayManager::fpsStats() const
{
    static const DisplayFpsStats empty;
    return m_current ? m_current->getFpsStats() : empty;
}

void DisplayManager::resetFpsStats()
{
    if (m_current != nullptr) {
        m_current->resetFpsStats();
    }
}

#include "display_driver_version.h"
#include "version_manager.h"
COMPONENT_VERSION_REGISTER(display_driver, DISPLAY_DRIVER_VERSION_STRING)
