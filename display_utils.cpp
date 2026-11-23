/**
 * @file     display_utils.cpp
 * @brief    显示工具组件实现 — C++ class
 *
 * 功能：LVGL 显示辅助工具实现
 * 修改：2026-11-21 由 C API（display_* 函数）重构为 DisplayUtils 静态类
 */
#include "display_utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "display_utils";

/* 格式化文本缓冲大小 */
#define FMT_BUF_SIZE 256

void DisplayUtils::clearScreen()
{
    lv_obj_t *scr = lv_scr_act();
    if (scr != NULL) {
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_invalidate(scr);
    }
}

lv_obj_t *DisplayUtils::chineseText(const char *text, int x, int y, int fontSize)
{
    lv_obj_t *label = lv_label_create(lv_scr_act());
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, x, y);

    /* 中文字体：优先用外部字库（需工程提供），否则退回默认字体 */
    LV_FONT_DECLARE(font_puhui_basic_14_1);
    lv_obj_set_style_text_font(label, &font_puhui_basic_14_1, 0);
    (void)fontSize;

    return label;
}

lv_obj_t *DisplayUtils::englishText(const char *text, int x, int y, int fontSize)
{
    lv_obj_t *label = lv_label_create(lv_scr_act());
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, x, y);

    /* 按字号选择 Montserrat 字体（需 lv_conf 开启对应字号） */
    switch (fontSize) {
        case 12:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
            break;
        case 16:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
            break;
        case 20:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
            break;
        case 24:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
            break;
        default:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
            break;
    }
    return label;
}

lv_obj_t *DisplayUtils::text(const char *text, int x, int y, int fontSize, TextType type)
{
    switch (type) {
        case TextType::CHINESE:
            return chineseText(text, x, y, fontSize);
        case TextType::ENGLISH:
            return englishText(text, x, y, fontSize);
        default:
            return englishText(text, x, y, fontSize);
    }
}

TextType DisplayUtils::detectTextType(const char *text)
{
    if (text == NULL) {
        return TextType::ENGLISH;
    }
    /* UTF-8 中文首字节 >= 0xE0；含任意高字节即判为中文 */
    for (const unsigned char *p = (const unsigned char *)text; *p != 0; p++) {
        if (*p >= 0x80) {
            return TextType::CHINESE;
        }
    }
    return TextType::ENGLISH;
}

lv_obj_t *DisplayUtils::createLabel(lv_obj_t *parent, TextType type, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text ? text : "");

    switch (type) {
        case TextType::CHINESE: {
            LV_FONT_DECLARE(font_puhui_basic_14_1);
            lv_obj_set_style_text_font(label, &font_puhui_basic_14_1, 0);
            break;
        }
        case TextType::ICON: {
            LV_FONT_DECLARE(font_awesome_14_1);
            lv_obj_set_style_text_font(label, &font_awesome_14_1, 0);
            break;
        }
        case TextType::ENGLISH:
        default:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
            break;
    }
    return label;
}

lv_obj_t *DisplayUtils::createLabelFmt(lv_obj_t *parent, TextType type, const char *fmt, ...)
{
    char buf[FMT_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return createLabel(parent, type, buf);
}

lv_obj_t *DisplayUtils::createRichText(lv_obj_t *parent, const char *text)
{
    /* 富文本：检测类型后按中文/图标字体渲染（简化实现） */
    TextType type = detectTextType(text);
    return createLabel(parent, type, text);
}

lv_obj_t *DisplayUtils::createRichTextFmt(lv_obj_t *parent, const char *fmt, ...)
{
    char buf[FMT_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return createRichText(parent, buf);
}
