/**
 * @file     display_utils.h
 * @brief    显示工具组件 — C++ class
 *
 * 功能：LVGL 显示辅助工具：文本类型检测（中文/英文/图标）、
 *       标签创建（普通/格式化/富文本）、清屏。
 * 修改：2026-11-21 由 C API（display_* 函数）重构为 DisplayUtils 静态类
 */
#ifndef DISPLAY_UTILS_H
#define DISPLAY_UTILS_H

#include "display_driver.h"
#include "lvgl.h"

/**
 * 显示工具类（纯静态工具类）
 */
class DisplayUtils {
public:
    /** 清屏（填充当前屏幕背景色） */
    static void clearScreen();

    /**
     * @brief  创建中文标签
     *
     * @param text     UTF-8 文本
     * @param x        X 坐标
     * @param y        Y 坐标
     * @param fontSize 字号（0 用默认中文字体）
     */
    static lv_obj_t *chineseText(const char *text, int x, int y, int fontSize);

    /** 创建英文标签（参数同 chineseText） */
    static lv_obj_t *englishText(const char *text, int x, int y, int fontSize);

    /** 创建标签并按 type 选字体 */
    static lv_obj_t *text(const char *text, int x, int y, int fontSize, TextType type);

    /**
     * @brief  检测文本类型
     *
     * 功能：含高字节（>=0x80）判为中文，否则英文
     */
    static TextType detectTextType(const char *text);

    /**
     * @brief  创建标签（按类型自动选字体）
     *
     * @param parent 父对象
     * @param type   文本类型
     * @param text   UTF-8 文本
     */
    static lv_obj_t *createLabel(lv_obj_t *parent, TextType type, const char *text);

    /** 创建格式化标签（printf 风格） */
    static lv_obj_t *createLabelFmt(lv_obj_t *parent, TextType type, const char *fmt, ...)
        __attribute__((format(printf, 3, 4)));

    /**
     * @brief  创建富文本标签（支持内嵌样式控制符）
     *
     * @param parent 父对象
     * @param text   富文本
     */
    static lv_obj_t *createRichText(lv_obj_t *parent, const char *text);

    /** 创建格式化富文本标签（printf 风格） */
    static lv_obj_t *createRichTextFmt(lv_obj_t *parent, const char *fmt, ...)
        __attribute__((format(printf, 2, 3)));
};

#endif /* DISPLAY_UTILS_H */
