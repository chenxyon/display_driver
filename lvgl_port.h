/**
 * @file     lvgl_port.h
 * @brief    LVGL 移植层组件 — C++ class（单例）
 *
 * 功能：管理 LVGL 初始化、多显示设备创建与 LVGL 任务，
 *       支持最多 4 个显示设备同时工作，提供调度锁回调挂接。
 * 修改：2026-11-21 由 C API（lvgl_port_* 函数）重构为 LvglPort 单例类；
 *       修复原实现 init 与 start 重复创建 LVGL 任务的问题；
 *       刷屏回调经 lv_display user_data 直达驱动实例（原线性查表）
 */
#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "display_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

/**
 * LVGL 移植层单例
 *
 * 用法：
 *   LvglPort::instance().init(DisplayType::ILI9341_TFT_HOR, pins);
 *   LvglPort::instance().start();   // 启动 LVGL 任务
 */
class LvglPort {
public:
    using LockFn = void (*)(); /*!< 调度锁回调 */

    /* 全局唯一实例 */
    static LvglPort &instance();

    /**
     * @brief  初始化单个显示设备（不启动任务）
     *
     * @param type 显示设备类型
     * @param pins 引脚配置
     * @return true 成功
     */
    bool init(DisplayType type, const DisplayPinConfig &pins);

    /**
     * @brief  初始化多个显示设备（不启动任务）
     *
     * @param types 类型数组
     * @param pins  引脚数组（与 types 一一对应）
     * @param count 设备数量（<= MAX_DISPLAYS）
     * @return true 全部成功
     */
    bool initMulti(const DisplayType *types, const DisplayPinConfig *pins, int count);

    /** 启动 LVGL 任务（重复调用无效） */
    bool start();

    /** 停止 LVGL 任务 */
    bool stop();

    /** 释放全部显示设备与缓冲 */
    bool deinit();

    /* ---- 显示设备访问 ---- */
    lv_display_t *getDisplay() const { return m_currentDisplay; }
    lv_display_t *getDisplayById(int id) const;
    int getDisplayCount() const { return m_displayCount; }

    /* ---- 调度锁 ---- */
    void setLockFunctions(LockFn lockFn, LockFn unlockFn);
    LockFn getLockFn() const { return m_lockFn; }
    LockFn getUnlockFn() const { return m_unlockFn; }
    void lock();
    void unlock();

    /* LVGL 版本字符串 */
    static const char *version();

private:
    LvglPort() = default;

    static constexpr int MAX_DISPLAYS = 4; /*!< 最大显示设备数 */

    /** 单个显示设备条目 */
    struct DisplayEntry {
        lv_display_t *display = nullptr;
        DisplayDriver *driver = nullptr; /* 驱动实例（user_data 直达） */
        void *buf1 = nullptr;            /* 手动分配的绘制缓冲 1 */
        void *buf2 = nullptr;            /* 手动分配的绘制缓冲 2（双缓冲时） */
        DisplayType type = DisplayType::ILI9341_TFT_HOR;
        bool initialized = false;
    };

    /* LVGL 回调入口（thunk） */
    static void flushThunk(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap);
    static void rounderThunk(lv_display_t *disp, lv_area_t *area);
    static void taskThunk(void *arg);
    void taskLoop();

    bool initDisplay(DisplayType type, const DisplayPinConfig &pins);

    DisplayEntry m_displays[MAX_DISPLAYS] = {};
    int m_displayCount = 0;
    lv_display_t *m_currentDisplay = nullptr;
    TaskHandle_t m_taskHandle = nullptr;
    bool m_taskRunning = false;
    bool m_lvglInitialized = false;
    LockFn m_lockFn = nullptr;
    LockFn m_unlockFn = nullptr;
};

#endif /* LVGL_PORT_H */
