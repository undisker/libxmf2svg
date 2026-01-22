/**
 * @file wmf2svg.c
 * @brief WMF (Windows Metafile) to SVG conversion implementation
 *
 * Converts WMF files to SVG format using libuemf for parsing.
 */

#include "wmf2svg.h"
#include "wmf2svg_private.h"
#include "uwmf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <internal-fmem.h>

/* =========================================================================
 * Utility Functions
 * ========================================================================= */

/**
 * @brief Get a unique ID for SVG elements
 */
int wmf_get_id(WMF_DRAWING_STATES *states) {
    return states->uniqId++;
}

/**
 * @brief Check if an address is outside the WMF content bounds
 */
bool wmf_checkOutOfBounds(WMF_DRAWING_STATES *states, uintptr_t address) {
    return (address > states->endAddress);
}

/**
 * @brief Base64 encoding for embedded images
 */
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *wmf_base64_encode(const unsigned char *data, size_t input_length,
                        size_t *output_length) {
    *output_length = 4 * ((input_length + 2) / 3);
    char *encoded_data = (char *)malloc(*output_length + 1);
    if (encoded_data == NULL) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        encoded_data[j++] = base64_chars[(triple >> 18) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 12) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 6) & 0x3F];
        encoded_data[j++] = base64_chars[triple & 0x3F];
    }

    static const int mod_table[] = {0, 2, 1};
    for (i = 0; i < (size_t)mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';

    encoded_data[*output_length] = '\0';
    return encoded_data;
}

/* =========================================================================
 * Device Context Management
 * ========================================================================= */

/**
 * @brief Initialize a device context with default values
 */
static void wmf_initDeviceContext(WMF_DEVICE_CONTEXT *dc) {
    memset(dc, 0, sizeof(WMF_DEVICE_CONTEXT));

    /* Default pen: black, solid, 1px */
    dc->stroke_set = true;
    dc->stroke_style = WMF_PS_SOLID;
    dc->stroke_red = 0;
    dc->stroke_green = 0;
    dc->stroke_blue = 0;
    dc->stroke_width = 1.0;

    /* Default brush: white, solid */
    dc->fill_set = true;
    dc->fill_style = WMF_BS_SOLID;
    dc->fill_red = 255;
    dc->fill_green = 255;
    dc->fill_blue = 255;

    /* Default text: black */
    dc->text_red = 0;
    dc->text_green = 0;
    dc->text_blue = 0;
    dc->text_align = WMF_TA_LEFT | WMF_TA_TOP;

    /* Default background: white, opaque */
    dc->bk_red = 255;
    dc->bk_green = 255;
    dc->bk_blue = 255;
    dc->bk_mode = WMF_OPAQUE;

    /* Default fill mode */
    dc->fill_polymode = WMF_ALTERNATE;

    /* Default ROP2 */
    dc->rop2_mode = 13; /* R2_COPYPEN */
}

/**
 * @brief Copy device context
 */
void wmf_copyDeviceContext(WMF_DEVICE_CONTEXT *dest, WMF_DEVICE_CONTEXT *src) {
    if (dest->font_name) {
        free(dest->font_name);
        dest->font_name = NULL;
    }
    memcpy(dest, src, sizeof(WMF_DEVICE_CONTEXT));
    if (src->font_name) {
        dest->font_name = strdup(src->font_name);
    }
}

/**
 * @brief Save device context to stack
 */
void wmf_saveDeviceContext(WMF_DRAWING_STATES *states) {
    WMF_DEVICE_CONTEXT_STACK *new_stack =
        (WMF_DEVICE_CONTEXT_STACK *)calloc(1, sizeof(WMF_DEVICE_CONTEXT_STACK));
    if (!new_stack) return;

    wmf_copyDeviceContext(&new_stack->DeviceContext, &states->currentDeviceContext);
    new_stack->previous = states->DeviceContextStack;
    states->DeviceContextStack = new_stack;
}

/**
 * @brief Restore device context from stack
 */
void wmf_restoreDeviceContext(WMF_DRAWING_STATES *states, int16_t index) {
    if (index == 0) return;

    int count = (index < 0) ? -index : index;

    for (int i = 0; i < count && states->DeviceContextStack; i++) {
        WMF_DEVICE_CONTEXT_STACK *old = states->DeviceContextStack;
        wmf_copyDeviceContext(&states->currentDeviceContext, &old->DeviceContext);
        states->DeviceContextStack = old->previous;
        wmf_freeDeviceContext(&old->DeviceContext);
        free(old);
    }
}

/**
 * @brief Free device context
 */
void wmf_freeDeviceContext(WMF_DEVICE_CONTEXT *dc) {
    if (dc->font_name) {
        free(dc->font_name);
        dc->font_name = NULL;
    }
}

/**
 * @brief Free device context stack
 */
void wmf_freeDeviceContextStack(WMF_DRAWING_STATES *states) {
    while (states->DeviceContextStack) {
        WMF_DEVICE_CONTEXT_STACK *old = states->DeviceContextStack;
        states->DeviceContextStack = old->previous;
        wmf_freeDeviceContext(&old->DeviceContext);
        free(old);
    }
}

/* =========================================================================
 * Object Table Management
 * ========================================================================= */

/**
 * @brief Create object in object table, returns index
 */
int wmf_createObject(WMF_DRAWING_STATES *states, WMF_GRAPH_OBJECT *obj) {
    /* Find first empty slot */
    for (int i = 0; i < states->objectTableSize; i++) {
        if (states->objectTable[i].type == WMF_OBJ_INVALID) {
            memcpy(&states->objectTable[i], obj, sizeof(WMF_GRAPH_OBJECT));
            if (obj->font_name) {
                states->objectTable[i].font_name = strdup(obj->font_name);
            }
            return i;
        }
    }
    return -1; /* Table full */
}

/**
 * @brief Delete object from table
 */
void wmf_deleteObject(WMF_DRAWING_STATES *states, uint16_t index) {
    if (index >= (uint16_t)states->objectTableSize) return;

    if (states->objectTable[index].font_name) {
        free(states->objectTable[index].font_name);
    }
    memset(&states->objectTable[index], 0, sizeof(WMF_GRAPH_OBJECT));
}

/**
 * @brief Select object into device context
 */
void wmf_selectObject(WMF_DRAWING_STATES *states, uint16_t index) {
    /* Check for stock objects */
    if (index & 0x80000000) {
        uint32_t stockObj = index;
        switch (stockObj) {
            case WMF_WHITE_BRUSH:
                states->currentDeviceContext.fill_set = true;
                states->currentDeviceContext.fill_style = WMF_BS_SOLID;
                states->currentDeviceContext.fill_red = 255;
                states->currentDeviceContext.fill_green = 255;
                states->currentDeviceContext.fill_blue = 255;
                break;
            case WMF_LTGRAY_BRUSH:
                states->currentDeviceContext.fill_set = true;
                states->currentDeviceContext.fill_style = WMF_BS_SOLID;
                states->currentDeviceContext.fill_red = 192;
                states->currentDeviceContext.fill_green = 192;
                states->currentDeviceContext.fill_blue = 192;
                break;
            case WMF_GRAY_BRUSH:
                states->currentDeviceContext.fill_set = true;
                states->currentDeviceContext.fill_style = WMF_BS_SOLID;
                states->currentDeviceContext.fill_red = 128;
                states->currentDeviceContext.fill_green = 128;
                states->currentDeviceContext.fill_blue = 128;
                break;
            case WMF_DKGRAY_BRUSH:
                states->currentDeviceContext.fill_set = true;
                states->currentDeviceContext.fill_style = WMF_BS_SOLID;
                states->currentDeviceContext.fill_red = 64;
                states->currentDeviceContext.fill_green = 64;
                states->currentDeviceContext.fill_blue = 64;
                break;
            case WMF_BLACK_BRUSH:
                states->currentDeviceContext.fill_set = true;
                states->currentDeviceContext.fill_style = WMF_BS_SOLID;
                states->currentDeviceContext.fill_red = 0;
                states->currentDeviceContext.fill_green = 0;
                states->currentDeviceContext.fill_blue = 0;
                break;
            case WMF_NULL_BRUSH:
                states->currentDeviceContext.fill_set = false;
                states->currentDeviceContext.fill_style = WMF_BS_NULL;
                break;
            case WMF_WHITE_PEN:
                states->currentDeviceContext.stroke_set = true;
                states->currentDeviceContext.stroke_style = WMF_PS_SOLID;
                states->currentDeviceContext.stroke_red = 255;
                states->currentDeviceContext.stroke_green = 255;
                states->currentDeviceContext.stroke_blue = 255;
                states->currentDeviceContext.stroke_width = 1.0;
                break;
            case WMF_BLACK_PEN:
                states->currentDeviceContext.stroke_set = true;
                states->currentDeviceContext.stroke_style = WMF_PS_SOLID;
                states->currentDeviceContext.stroke_red = 0;
                states->currentDeviceContext.stroke_green = 0;
                states->currentDeviceContext.stroke_blue = 0;
                states->currentDeviceContext.stroke_width = 1.0;
                break;
            case WMF_NULL_PEN:
                states->currentDeviceContext.stroke_set = false;
                states->currentDeviceContext.stroke_style = WMF_PS_NULL;
                break;
            default:
                /* Other stock objects - use defaults */
                break;
        }
        return;
    }

    if (index >= (uint16_t)states->objectTableSize) return;

    WMF_GRAPH_OBJECT *obj = &states->objectTable[index];

    switch (obj->type) {
        case WMF_OBJ_PEN:
            states->currentDeviceContext.stroke_set = obj->stroke_set;
            states->currentDeviceContext.stroke_style = obj->stroke_style;
            states->currentDeviceContext.stroke_red = obj->stroke_red;
            states->currentDeviceContext.stroke_green = obj->stroke_green;
            states->currentDeviceContext.stroke_blue = obj->stroke_blue;
            states->currentDeviceContext.stroke_width = obj->stroke_width;
            break;

        case WMF_OBJ_BRUSH:
            states->currentDeviceContext.fill_set = obj->fill_set;
            states->currentDeviceContext.fill_style = obj->fill_style;
            states->currentDeviceContext.fill_hatch = obj->fill_hatch;
            states->currentDeviceContext.fill_red = obj->fill_red;
            states->currentDeviceContext.fill_green = obj->fill_green;
            states->currentDeviceContext.fill_blue = obj->fill_blue;
            break;

        case WMF_OBJ_FONT:
            states->currentDeviceContext.font_set = obj->font_set;
            if (states->currentDeviceContext.font_name) {
                free(states->currentDeviceContext.font_name);
            }
            states->currentDeviceContext.font_name =
                obj->font_name ? strdup(obj->font_name) : NULL;
            states->currentDeviceContext.font_height = obj->font_height;
            states->currentDeviceContext.font_width = obj->font_width;
            states->currentDeviceContext.font_escapement = obj->font_escapement;
            states->currentDeviceContext.font_orientation = obj->font_orientation;
            states->currentDeviceContext.font_weight = obj->font_weight;
            states->currentDeviceContext.font_italic = obj->font_italic;
            states->currentDeviceContext.font_underline = obj->font_underline;
            states->currentDeviceContext.font_strikeout = obj->font_strikeout;
            states->currentDeviceContext.font_charset = obj->font_charset;
            break;

        default:
            break;
    }
}

/**
 * @brief Free object table
 */
void wmf_freeObjectTable(WMF_DRAWING_STATES *states) {
    if (states->objectTable) {
        for (int i = 0; i < states->objectTableSize; i++) {
            if (states->objectTable[i].font_name) {
                free(states->objectTable[i].font_name);
            }
        }
        free(states->objectTable);
        states->objectTable = NULL;
    }
}

/* =========================================================================
 * Coordinate Transformation
 * ========================================================================= */

/**
 * @brief Scale X coordinate
 */
double wmf_scaleX(WMF_DRAWING_STATES *states, int16_t x) {
    double result = (double)x;

    if (states->windowExtX != 0) {
        result = (result - states->windowOrgX) *
                 ((double)states->viewportExtX / (double)states->windowExtX) +
                 states->viewportOrgX;
    }

    result *= states->scaling;
    return result;
}

/**
 * @brief Scale Y coordinate
 */
double wmf_scaleY(WMF_DRAWING_STATES *states, int16_t y) {
    double result = (double)y;

    if (states->windowExtY != 0) {
        result = (result - states->windowOrgY) *
                 ((double)states->viewportExtY / (double)states->windowExtY) +
                 states->viewportOrgY;
    }

    result *= states->scaling;
    return result;
}

/**
 * @brief Scale a point
 */
WMF_POINT_D wmf_point_scale(WMF_DRAWING_STATES *states, int16_t x, int16_t y) {
    WMF_POINT_D pt;
    pt.x = wmf_scaleX(states, x);
    pt.y = wmf_scaleY(states, y);
    return pt;
}

/* =========================================================================
 * SVG Style Helpers
 * ========================================================================= */

/**
 * @brief Output stroke style attributes
 */
void wmf_stroke_style(WMF_DRAWING_STATES *states, FILE *out) {
    if (!states->currentDeviceContext.stroke_set ||
        states->currentDeviceContext.stroke_style == WMF_PS_NULL) {
        fprintf(out, "stroke=\"none\" ");
        return;
    }

    fprintf(out, "stroke=\"#%02X%02X%02X\" ",
            states->currentDeviceContext.stroke_red,
            states->currentDeviceContext.stroke_green,
            states->currentDeviceContext.stroke_blue);

    double width = states->currentDeviceContext.stroke_width * states->scaling;
    if (width < 1.0) width = 1.0;
    fprintf(out, "stroke-width=\"%.2f\" ", width);

    /* Stroke dash pattern */
    switch (states->currentDeviceContext.stroke_style & 0x0F) {
        case WMF_PS_DASH:
            fprintf(out, "stroke-dasharray=\"%.0f,%.0f\" ", width * 3, width);
            break;
        case WMF_PS_DOT:
            fprintf(out, "stroke-dasharray=\"%.0f,%.0f\" ", width, width);
            break;
        case WMF_PS_DASHDOT:
            fprintf(out, "stroke-dasharray=\"%.0f,%.0f,%.0f,%.0f\" ",
                    width * 3, width, width, width);
            break;
        case WMF_PS_DASHDOTDOT:
            fprintf(out, "stroke-dasharray=\"%.0f,%.0f,%.0f,%.0f,%.0f,%.0f\" ",
                    width * 3, width, width, width, width, width);
            break;
        default:
            break;
    }
}

/**
 * @brief Output fill style attributes
 */
void wmf_fill_style(WMF_DRAWING_STATES *states, FILE *out) {
    if (!states->currentDeviceContext.fill_set ||
        states->currentDeviceContext.fill_style == WMF_BS_NULL ||
        states->currentDeviceContext.fill_style == WMF_BS_HOLLOW) {
        fprintf(out, "fill=\"none\" ");
        return;
    }

    fprintf(out, "fill=\"#%02X%02X%02X\" ",
            states->currentDeviceContext.fill_red,
            states->currentDeviceContext.fill_green,
            states->currentDeviceContext.fill_blue);

    /* Fill rule */
    if (states->currentDeviceContext.fill_polymode == WMF_WINDING) {
        fprintf(out, "fill-rule=\"nonzero\" ");
    } else {
        fprintf(out, "fill-rule=\"evenodd\" ");
    }
}

/* =========================================================================
 * Record Processing
 * ========================================================================= */

/**
 * @brief Process a single WMF record
 */
int wmf_onerec_draw(const char *contents, const char *blimit, int recnum,
                    FILE *out, WMF_DRAWING_STATES *states) {
    UNUSED(blimit);

    /* Get record size and type */
    uint32_t size;
    memcpy(&size, contents, sizeof(uint32_t));
    size *= 2; /* Size is in 16-bit words, convert to bytes */

    /* iType is the enum index (0-255), xb is the extra byte */
    uint8_t iType = *(contents + 4);
    uint8_t xb = *(contents + 5);
    uint16_t funcNum = (xb << 8) | iType;

    wmf_verbose_printf("\n%-4d Record: 0x%04X (size=%u)\n", recnum, funcNum, size);

    switch (iType) {
        case U_WMR_EOF: {
            wmf_verbose_printf("   Type: EOF\n");
            WMF_FLAG_SUPPORTED;
            return 0; /* Signal end */
        }

        case U_WMR_SETBKCOLOR: {
            U_COLORREF color;
            if (U_WMRSETBKCOLOR_get(contents, &color) > 0) {
                states->currentDeviceContext.bk_red = color.Red;
                states->currentDeviceContext.bk_green = color.Green;
                states->currentDeviceContext.bk_blue = color.Blue;
                wmf_verbose_printf("   Type: SETBKCOLOR (#%02X%02X%02X)\n",
                                   color.Red, color.Green, color.Blue);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SETBKMODE: {
            uint16_t mode;
            if (U_WMRSETBKMODE_get(contents, &mode) > 0) {
                states->currentDeviceContext.bk_mode = mode;
                wmf_verbose_printf("   Type: SETBKMODE (%d)\n", mode);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SETMAPMODE: {
            uint16_t mode;
            if (U_WMRSETMAPMODE_get(contents, &mode) > 0) {
                states->mapMode = mode;
                wmf_verbose_printf("   Type: SETMAPMODE (%d)\n", mode);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SETROP2: {
            uint16_t mode;
            if (U_WMRSETROP2_get(contents, &mode) > 0) {
                states->currentDeviceContext.rop2_mode = mode;
                wmf_verbose_printf("   Type: SETROP2 (%d)\n", mode);
                WMF_FLAG_PARTIAL;
            }
            break;
        }

        case U_WMR_SETPOLYFILLMODE: {
            uint16_t mode;
            if (U_WMRSETPOLYFILLMODE_get(contents, &mode) > 0) {
                states->currentDeviceContext.fill_polymode = mode;
                wmf_verbose_printf("   Type: SETPOLYFILLMODE (%d)\n", mode);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SETTEXTCOLOR: {
            U_COLORREF color;
            if (U_WMRSETTEXTCOLOR_get(contents, &color) > 0) {
                states->currentDeviceContext.text_red = color.Red;
                states->currentDeviceContext.text_green = color.Green;
                states->currentDeviceContext.text_blue = color.Blue;
                wmf_verbose_printf("   Type: SETTEXTCOLOR (#%02X%02X%02X)\n",
                                   color.Red, color.Green, color.Blue);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SETTEXTALIGN: {
            uint16_t align;
            if (U_WMRSETTEXTALIGN_get(contents, &align) > 0) {
                states->currentDeviceContext.text_align = align;
                wmf_verbose_printf("   Type: SETTEXTALIGN (0x%04X)\n", align);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SETWINDOWORG: {
            U_POINT16 pt;
            if (U_WMRSETWINDOWORG_get(contents, &pt) > 0) {
                states->windowOrgX = pt.x;
                states->windowOrgY = pt.y;
                wmf_verbose_printf("   Type: SETWINDOWORG (%d, %d)\n", pt.x, pt.y);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SETWINDOWEXT: {
            U_POINT16 pt;
            if (U_WMRSETWINDOWEXT_get(contents, &pt) > 0) {
                states->windowExtX = pt.x;
                states->windowExtY = pt.y;
                wmf_verbose_printf("   Type: SETWINDOWEXT (%d, %d)\n", pt.x, pt.y);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SETVIEWPORTORG: {
            U_POINT16 pt;
            if (U_WMRSETVIEWPORTORG_get(contents, &pt) > 0) {
                states->viewportOrgX = pt.x;
                states->viewportOrgY = pt.y;
                wmf_verbose_printf("   Type: SETVIEWPORTORG (%d, %d)\n", pt.x, pt.y);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SETVIEWPORTEXT: {
            U_POINT16 pt;
            if (U_WMRSETVIEWPORTEXT_get(contents, &pt) > 0) {
                states->viewportExtX = pt.x;
                states->viewportExtY = pt.y;
                wmf_verbose_printf("   Type: SETVIEWPORTEXT (%d, %d)\n", pt.x, pt.y);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SAVEDC: {
            if (U_WMRSAVEDC_get(contents) > 0) {
                wmf_saveDeviceContext(states);
                wmf_verbose_printf("   Type: SAVEDC\n");
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_RESTOREDC: {
            int16_t dc;
            if (U_WMRRESTOREDC_get(contents, &dc) > 0) {
                wmf_restoreDeviceContext(states, dc);
                wmf_verbose_printf("   Type: RESTOREDC (%d)\n", dc);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_SELECTOBJECT: {
            uint16_t obj;
            if (U_WMRSELECTOBJECT_get(contents, &obj) > 0) {
                wmf_selectObject(states, obj);
                wmf_verbose_printf("   Type: SELECTOBJECT (%d)\n", obj);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_DELETEOBJECT: {
            uint16_t obj;
            if (U_WMRDELETEOBJECT_get(contents, &obj) > 0) {
                wmf_deleteObject(states, obj);
                wmf_verbose_printf("   Type: DELETEOBJECT (%d)\n", obj);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_CREATEPENINDIRECT: {
            U_PEN pen;
            if (U_WMRCREATEPENINDIRECT_get(contents, &pen) > 0) {
                WMF_GRAPH_OBJECT obj;
                memset(&obj, 0, sizeof(obj));
                obj.type = WMF_OBJ_PEN;
                obj.stroke_set = (pen.Style != WMF_PS_NULL);
                obj.stroke_style = pen.Style;

                /* Width - handle unaligned access */
                int16_t width;
                memcpy(&width, pen.Widthw, sizeof(int16_t));
                obj.stroke_width = (width > 0) ? width : 1.0;

                /* Color - handle unaligned access */
                obj.stroke_red = pen.Color.Red;
                obj.stroke_green = pen.Color.Green;
                obj.stroke_blue = pen.Color.Blue;

                int idx = wmf_createObject(states, &obj);
                wmf_verbose_printf("   Type: CREATEPENINDIRECT -> obj %d (style=%d, width=%.0f, color=#%02X%02X%02X)\n",
                                   idx, pen.Style, obj.stroke_width,
                                   obj.stroke_red, obj.stroke_green, obj.stroke_blue);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_CREATEBRUSHINDIRECT: {
            const char *brush;
            if (U_WMRCREATEBRUSHINDIRECT_get(contents, &brush) > 0) {
                U_WLOGBRUSH lb;
                memcpy(&lb, brush, sizeof(U_WLOGBRUSH));

                WMF_GRAPH_OBJECT obj;
                memset(&obj, 0, sizeof(obj));
                obj.type = WMF_OBJ_BRUSH;
                obj.fill_set = (lb.Style != WMF_BS_NULL && lb.Style != WMF_BS_HOLLOW);
                obj.fill_style = lb.Style;
                obj.fill_hatch = lb.Hatch;
                obj.fill_red = lb.Color.Red;
                obj.fill_green = lb.Color.Green;
                obj.fill_blue = lb.Color.Blue;

                int idx = wmf_createObject(states, &obj);
                wmf_verbose_printf("   Type: CREATEBRUSHINDIRECT -> obj %d (style=%d, color=#%02X%02X%02X)\n",
                                   idx, lb.Style, obj.fill_red, obj.fill_green, obj.fill_blue);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_CREATEFONTINDIRECT: {
            const char *font;
            if (U_WMRCREATEFONTINDIRECT_get(contents, &font) > 0) {
                U_FONT *f = (U_FONT *)font;

                WMF_GRAPH_OBJECT obj;
                memset(&obj, 0, sizeof(obj));
                obj.type = WMF_OBJ_FONT;
                obj.font_set = true;
                obj.font_height = f->Height;
                obj.font_width = f->Width;
                obj.font_escapement = f->Escapement;
                obj.font_orientation = f->Orientation;
                obj.font_weight = f->Weight;
                obj.font_italic = f->Italic;
                obj.font_underline = f->Underline;
                obj.font_strikeout = f->StrikeOut;
                obj.font_charset = f->CharSet;

                /* Copy font name - variable length, null-terminated */
                obj.font_name = strdup((char *)f->FaceName);

                int idx = wmf_createObject(states, &obj);
                wmf_verbose_printf("   Type: CREATEFONTINDIRECT -> obj %d (name=%s, height=%d)\n",
                                   idx, obj.font_name ? obj.font_name : "(null)", obj.font_height);
                if (obj.font_name) free(obj.font_name);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_MOVETO: {
            U_POINT16 pt;
            if (U_WMRMOVETO_get(contents, &pt) > 0) {
                states->cur_x = wmf_scaleX(states, pt.x);
                states->cur_y = wmf_scaleY(states, pt.y);
                wmf_verbose_printf("   Type: MOVETO (%d, %d)\n", pt.x, pt.y);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_LINETO: {
            U_POINT16 pt;
            if (U_WMRLINETO_get(contents, &pt) > 0) {
                double x2 = wmf_scaleX(states, pt.x);
                double y2 = wmf_scaleY(states, pt.y);

                fprintf(out, "<%sline x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" ",
                        states->nameSpaceString, states->cur_x, states->cur_y, x2, y2);
                wmf_stroke_style(states, out);
                fprintf(out, "/>\n");

                states->cur_x = x2;
                states->cur_y = y2;

                wmf_verbose_printf("   Type: LINETO (%d, %d)\n", pt.x, pt.y);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_RECTANGLE: {
            U_RECT16 rect;
            if (U_WMRRECTANGLE_get(contents, &rect) > 0) {
                double x = wmf_scaleX(states, rect.left);
                double y = wmf_scaleY(states, rect.top);
                double w = wmf_scaleX(states, rect.right) - x;
                double h = wmf_scaleY(states, rect.bottom) - y;

                fprintf(out, "<%srect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" ",
                        states->nameSpaceString, x, y, w, h);
                wmf_fill_style(states, out);
                wmf_stroke_style(states, out);
                fprintf(out, "/>\n");

                wmf_verbose_printf("   Type: RECTANGLE (%d,%d)-(%d,%d)\n",
                                   rect.left, rect.top, rect.right, rect.bottom);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_ELLIPSE: {
            U_RECT16 rect;
            if (U_WMRELLIPSE_get(contents, &rect) > 0) {
                double x1 = wmf_scaleX(states, rect.left);
                double y1 = wmf_scaleY(states, rect.top);
                double x2 = wmf_scaleX(states, rect.right);
                double y2 = wmf_scaleY(states, rect.bottom);

                double cx = (x1 + x2) / 2.0;
                double cy = (y1 + y2) / 2.0;
                double rx = fabs(x2 - x1) / 2.0;
                double ry = fabs(y2 - y1) / 2.0;

                fprintf(out, "<%sellipse cx=\"%.2f\" cy=\"%.2f\" rx=\"%.2f\" ry=\"%.2f\" ",
                        states->nameSpaceString, cx, cy, rx, ry);
                wmf_fill_style(states, out);
                wmf_stroke_style(states, out);
                fprintf(out, "/>\n");

                wmf_verbose_printf("   Type: ELLIPSE (%d,%d)-(%d,%d)\n",
                                   rect.left, rect.top, rect.right, rect.bottom);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_ROUNDRECT: {
            int16_t width, height;
            U_RECT16 rect;
            if (U_WMRROUNDRECT_get(contents, &width, &height, &rect) > 0) {
                double x = wmf_scaleX(states, rect.left);
                double y = wmf_scaleY(states, rect.top);
                double w = wmf_scaleX(states, rect.right) - x;
                double h = wmf_scaleY(states, rect.bottom) - y;
                double rx = fabs(width * states->scaling) / 2.0;
                double ry = fabs(height * states->scaling) / 2.0;

                fprintf(out, "<%srect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" rx=\"%.2f\" ry=\"%.2f\" ",
                        states->nameSpaceString, x, y, w, h, rx, ry);
                wmf_fill_style(states, out);
                wmf_stroke_style(states, out);
                fprintf(out, "/>\n");

                wmf_verbose_printf("   Type: ROUNDRECT (%d,%d)-(%d,%d) r=(%d,%d)\n",
                                   rect.left, rect.top, rect.right, rect.bottom, width, height);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_POLYGON: {
            uint16_t numPoints;
            const char *data;
            if (U_WMRPOLYGON_get(contents, &numPoints, &data) > 0 && numPoints > 0) {
                fprintf(out, "<%spolygon points=\"", states->nameSpaceString);

                for (uint16_t i = 0; i < numPoints; i++) {
                    U_POINT16 pt;
                    memcpy(&pt, data + i * sizeof(U_POINT16), sizeof(U_POINT16));
                    double x = wmf_scaleX(states, pt.x);
                    double y = wmf_scaleY(states, pt.y);
                    fprintf(out, "%.2f,%.2f ", x, y);
                }

                fprintf(out, "\" ");
                wmf_fill_style(states, out);
                wmf_stroke_style(states, out);
                fprintf(out, "/>\n");

                wmf_verbose_printf("   Type: POLYGON (%d points)\n", numPoints);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_POLYLINE: {
            uint16_t numPoints;
            const char *data;
            if (U_WMRPOLYLINE_get(contents, &numPoints, &data) > 0 && numPoints > 0) {
                fprintf(out, "<%spolyline points=\"", states->nameSpaceString);

                for (uint16_t i = 0; i < numPoints; i++) {
                    U_POINT16 pt;
                    memcpy(&pt, data + i * sizeof(U_POINT16), sizeof(U_POINT16));
                    double x = wmf_scaleX(states, pt.x);
                    double y = wmf_scaleY(states, pt.y);
                    fprintf(out, "%.2f,%.2f ", x, y);
                }

                fprintf(out, "\" fill=\"none\" ");
                wmf_stroke_style(states, out);
                fprintf(out, "/>\n");

                wmf_verbose_printf("   Type: POLYLINE (%d points)\n", numPoints);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_POLYPOLYGON: {
            uint16_t nPolys;
            const uint16_t *aPolyCounts;
            const char *Points;
            if (U_WMRPOLYPOLYGON_get(contents, &nPolys, &aPolyCounts, &Points) > 0) {
                const char *pointPtr = Points;

                for (uint16_t p = 0; p < nPolys; p++) {
                    uint16_t numPoints = aPolyCounts[p];
                    if (numPoints == 0) continue;

                    fprintf(out, "<%spolygon points=\"", states->nameSpaceString);

                    for (uint16_t i = 0; i < numPoints; i++) {
                        U_POINT16 pt;
                        memcpy(&pt, pointPtr, sizeof(U_POINT16));
                        pointPtr += sizeof(U_POINT16);

                        double x = wmf_scaleX(states, pt.x);
                        double y = wmf_scaleY(states, pt.y);
                        fprintf(out, "%.2f,%.2f ", x, y);
                    }

                    fprintf(out, "\" ");
                    wmf_fill_style(states, out);
                    wmf_stroke_style(states, out);
                    fprintf(out, "/>\n");
                }

                wmf_verbose_printf("   Type: POLYPOLYGON (%d polygons)\n", nPolys);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_ARC:
        case U_WMR_CHORD:
        case U_WMR_PIE: {
            U_POINT16 StartArc, EndArc;
            U_RECT16 rect;
            int result;

            if (iType == U_WMR_ARC) {
                result = U_WMRARC_get(contents, &StartArc, &EndArc, &rect);
            } else if (iType == U_WMR_CHORD) {
                result = U_WMRCHORD_get(contents, &StartArc, &EndArc, &rect);
            } else {
                result = U_WMRPIE_get(contents, &StartArc, &EndArc, &rect);
            }

            if (result > 0) {
                double x1 = wmf_scaleX(states, rect.left);
                double y1 = wmf_scaleY(states, rect.top);
                double x2 = wmf_scaleX(states, rect.right);
                double y2 = wmf_scaleY(states, rect.bottom);

                double cx = (x1 + x2) / 2.0;
                double cy = (y1 + y2) / 2.0;
                double rx = fabs(x2 - x1) / 2.0;
                double ry = fabs(y2 - y1) / 2.0;

                /* Calculate start and end angles */
                double startX = wmf_scaleX(states, StartArc.x);
                double startY = wmf_scaleY(states, StartArc.y);
                double endX = wmf_scaleX(states, EndArc.x);
                double endY = wmf_scaleY(states, EndArc.y);

                double startAngle = atan2(startY - cy, startX - cx);
                double endAngle = atan2(endY - cy, endX - cx);

                /* Convert to SVG arc parameters */
                double sx = cx + rx * cos(startAngle);
                double sy = cy + ry * sin(startAngle);
                double ex = cx + rx * cos(endAngle);
                double ey = cy + ry * sin(endAngle);

                /* Determine large arc flag */
                double angleDiff = endAngle - startAngle;
                if (angleDiff < 0) angleDiff += 2 * 3.14159265358979323846;
                int largeArc = (angleDiff > 3.14159265358979323846) ? 1 : 0;

                fprintf(out, "<%spath d=\"", states->nameSpaceString);

                if (iType == U_WMR_PIE) {
                    fprintf(out, "M %.2f,%.2f L %.2f,%.2f ", cx, cy, sx, sy);
                } else {
                    fprintf(out, "M %.2f,%.2f ", sx, sy);
                }

                fprintf(out, "A %.2f,%.2f 0 %d,1 %.2f,%.2f ", rx, ry, largeArc, ex, ey);

                if (iType == U_WMR_PIE) {
                    fprintf(out, "Z");
                } else if (iType == U_WMR_CHORD) {
                    fprintf(out, "Z");
                }

                fprintf(out, "\" ");

                if (iType == U_WMR_ARC) {
                    fprintf(out, "fill=\"none\" ");
                } else {
                    wmf_fill_style(states, out);
                }
                wmf_stroke_style(states, out);
                fprintf(out, "/>\n");

                const char *typeName = (iType == U_WMR_ARC) ? "ARC" :
                                       (iType == U_WMR_CHORD) ? "CHORD" : "PIE";
                wmf_verbose_printf("   Type: %s\n", typeName);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_TEXTOUT: {
            U_POINT16 Dst;
            int16_t Length;
            const char *string;
            if (U_WMRTEXTOUT_get(contents, &Dst, &Length, &string) > 0 && Length > 0) {
                double x = wmf_scaleX(states, Dst.x);
                double y = wmf_scaleY(states, Dst.y);

                /* Allocate buffer for text + null terminator */
                char *text = (char *)malloc(Length + 1);
                if (text) {
                    memcpy(text, string, Length);
                    text[Length] = '\0';

                    /* Calculate font size */
                    double fontSize = fabs(states->currentDeviceContext.font_height) *
                                      states->scaling;
                    if (fontSize < 1.0) fontSize = 12.0;

                    /* Text anchor based on alignment */
                    const char *anchor = "start";
                    if (states->currentDeviceContext.text_align & WMF_TA_CENTER) {
                        anchor = "middle";
                    } else if (states->currentDeviceContext.text_align & WMF_TA_RIGHT) {
                        anchor = "end";
                    }

                    fprintf(out, "<%stext x=\"%.2f\" y=\"%.2f\" ",
                            states->nameSpaceString, x, y);
                    fprintf(out, "fill=\"#%02X%02X%02X\" ",
                            states->currentDeviceContext.text_red,
                            states->currentDeviceContext.text_green,
                            states->currentDeviceContext.text_blue);
                    fprintf(out, "font-size=\"%.2f\" ", fontSize);
                    fprintf(out, "text-anchor=\"%s\" ", anchor);

                    if (states->currentDeviceContext.font_name) {
                        fprintf(out, "font-family=\"%s\" ",
                                states->currentDeviceContext.font_name);
                    }
                    if (states->currentDeviceContext.font_italic) {
                        fprintf(out, "font-style=\"italic\" ");
                    }
                    if (states->currentDeviceContext.font_weight > 400) {
                        fprintf(out, "font-weight=\"bold\" ");
                    }

                    /* Escape XML special characters */
                    fprintf(out, ">");
                    for (int i = 0; i < Length && text[i]; i++) {
                        switch (text[i]) {
                            case '<': fprintf(out, "&lt;"); break;
                            case '>': fprintf(out, "&gt;"); break;
                            case '&': fprintf(out, "&amp;"); break;
                            case '"': fprintf(out, "&quot;"); break;
                            default: fputc(text[i], out); break;
                        }
                    }
                    fprintf(out, "</%stext>\n", states->nameSpaceString);

                    free(text);
                }

                wmf_verbose_printf("   Type: TEXTOUT at (%d,%d)\n", Dst.x, Dst.y);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        case U_WMR_EXTTEXTOUT: {
            U_POINT16 Dst;
            int16_t Length;
            uint16_t Opts;
            const char *string;
            const int16_t *dx;
            U_RECT16 rect;

            if (U_WMREXTTEXTOUT_get(contents, &Dst, &Length, &Opts, &string, &dx, &rect) > 0 && Length > 0) {
                double x = wmf_scaleX(states, Dst.x);
                double y = wmf_scaleY(states, Dst.y);

                /* Allocate buffer for text + null terminator */
                char *text = (char *)malloc(Length + 1);
                if (text) {
                    memcpy(text, string, Length);
                    text[Length] = '\0';

                    /* Calculate font size */
                    double fontSize = fabs(states->currentDeviceContext.font_height) *
                                      states->scaling;
                    if (fontSize < 1.0) fontSize = 12.0;

                    /* Text anchor based on alignment */
                    const char *anchor = "start";
                    if (states->currentDeviceContext.text_align & WMF_TA_CENTER) {
                        anchor = "middle";
                    } else if (states->currentDeviceContext.text_align & WMF_TA_RIGHT) {
                        anchor = "end";
                    }

                    fprintf(out, "<%stext x=\"%.2f\" y=\"%.2f\" ",
                            states->nameSpaceString, x, y);
                    fprintf(out, "fill=\"#%02X%02X%02X\" ",
                            states->currentDeviceContext.text_red,
                            states->currentDeviceContext.text_green,
                            states->currentDeviceContext.text_blue);
                    fprintf(out, "font-size=\"%.2f\" ", fontSize);
                    fprintf(out, "text-anchor=\"%s\" ", anchor);

                    if (states->currentDeviceContext.font_name) {
                        fprintf(out, "font-family=\"%s\" ",
                                states->currentDeviceContext.font_name);
                    }
                    if (states->currentDeviceContext.font_italic) {
                        fprintf(out, "font-style=\"italic\" ");
                    }
                    if (states->currentDeviceContext.font_weight > 400) {
                        fprintf(out, "font-weight=\"bold\" ");
                    }

                    /* Escape XML special characters */
                    fprintf(out, ">");
                    for (int i = 0; i < Length && text[i]; i++) {
                        switch (text[i]) {
                            case '<': fprintf(out, "&lt;"); break;
                            case '>': fprintf(out, "&gt;"); break;
                            case '&': fprintf(out, "&amp;"); break;
                            case '"': fprintf(out, "&quot;"); break;
                            default: fputc(text[i], out); break;
                        }
                    }
                    fprintf(out, "</%stext>\n", states->nameSpaceString);

                    free(text);
                }

                wmf_verbose_printf("   Type: EXTTEXTOUT at (%d,%d)\n", Dst.x, Dst.y);
                WMF_FLAG_SUPPORTED;
            }
            break;
        }

        /* Ignored records */
        case U_WMR_SETRELABS:
        case U_WMR_SETSTRETCHBLTMODE:
        case U_WMR_SETMAPPERFLAGS:
        case U_WMR_ESCAPE:
        case U_WMR_REALIZEPALETTE:
        case U_WMR_SELECTPALETTE:
        case U_WMR_CREATEPALETTE:
        case U_WMR_SETPALENTRIES:
        case U_WMR_RESIZEPALETTE:
        case U_WMR_ANIMATEPALETTE:
            wmf_verbose_printf("   Type: (ignored record 0x%04X)\n", funcNum);
            WMF_FLAG_IGNORED;
            break;

        default:
            wmf_verbose_printf("   Type: UNKNOWN (0x%04X)\n", funcNum);
            WMF_FLAG_IGNORED;
            break;
    }

    return size;
}

/* =========================================================================
 * Main Conversion Function
 * ========================================================================= */

/**
 * @brief Check if content is a valid WMF file
 */
int wmf2svg_is_wmf(char *contents, size_t length, bool *is_wmf) {
    if (!contents || length < 18 || !is_wmf) {
        if (is_wmf) *is_wmf = false;
        return -1;
    }

    *is_wmf = false;

    /* Check for placeable header (optional) */
    uint32_t key;
    memcpy(&key, contents, sizeof(uint32_t));

    if (key == 0x9AC6CDD7) {
        /* Has placeable header, check WMF header after it */
        if (length < 22 + 18) return 0;

        uint8_t iType = *(contents + 22);
        uint16_t version;
        memcpy(&version, contents + 24, sizeof(uint16_t));

        if (iType == 1 && (version == 0x0100 || version == 0x0300)) {
            *is_wmf = true;
        }
    } else {
        /* No placeable header, check WMF header directly */
        uint8_t iType = *contents;
        uint16_t version;
        memcpy(&version, contents + 4, sizeof(uint16_t));

        if (iType == 1 && (version == 0x0100 || version == 0x0300)) {
            *is_wmf = true;
        }
    }

    return 0;
}

/**
 * @brief Main WMF to SVG conversion function
 */
int wmf2svg(char *contents, size_t length, char **out, size_t *out_length,
            wmfGeneratorOptions *options) {
    if (!contents || !out || !out_length || !options) {
        return -1;
    }

    *out = NULL;
    *out_length = 0;

    /* Verify this is a WMF file */
    bool is_wmf;
    wmf2svg_is_wmf(contents, length, &is_wmf);
    if (!is_wmf) {
        return -2;
    }

    /* Parse headers */
    U_WMRPLACEABLE Placeable;
    U_WMRHEADER Header;

    int ret = wmfheader_get(contents, contents + length, &Placeable, &Header);
    if (ret < 0) {
        return -3;
    }

    /* Calculate starting offset for records */
    const char *recStart;
    bool hasPlaceable = false;

    uint32_t key;
    memcpy(&key, contents, sizeof(uint32_t));
    if (key == 0x9AC6CDD7) {
        hasPlaceable = true;
        recStart = contents + 22 + (Header.Size16w * 2);
    } else {
        recStart = contents + (Header.Size16w * 2);
    }

    /* Initialize drawing states */
    WMF_DRAWING_STATES states;
    memset(&states, 0, sizeof(states));

    states.uniqId = 1;
    states.verbose = options->verbose;
    states.svgDelimiter = options->svgDelimiter;
    states.endAddress = (uint64_t)(uintptr_t)(contents + length);

    /* Set up namespace */
    if (options->nameSpace && strlen(options->nameSpace) > 0) {
        states.nameSpace = strdup(options->nameSpace);
        states.nameSpaceString = (char *)malloc(strlen(options->nameSpace) + 2);
        sprintf(states.nameSpaceString, "%s:", options->nameSpace);
    } else {
        states.nameSpace = strdup("");
        states.nameSpaceString = strdup("");
    }

    /* Initialize device context */
    wmf_initDeviceContext(&states.currentDeviceContext);

    /* Initialize object table */
    states.objectTableSize = Header.nObjects;
    if (states.objectTableSize > 0) {
        states.objectTable = (WMF_GRAPH_OBJECT *)calloc(
            states.objectTableSize, sizeof(WMF_GRAPH_OBJECT));
    }

    /* Set up coordinate system from placeable header or defaults */
    if (hasPlaceable) {
        states.hasPlaceable = true;
        states.placeableBounds = Placeable.Dst;
        states.placeableInch = Placeable.Inch;

        states.windowOrgX = Placeable.Dst.left;
        states.windowOrgY = Placeable.Dst.top;
        states.windowExtX = Placeable.Dst.right - Placeable.Dst.left;
        states.windowExtY = Placeable.Dst.bottom - Placeable.Dst.top;

        /* Calculate scaling */
        double wmfWidth = (double)states.windowExtX;
        double wmfHeight = (double)states.windowExtY;

        if (options->imgWidth > 0 && options->imgHeight > 0) {
            states.imgWidth = options->imgWidth;
            states.imgHeight = options->imgHeight;
            double scaleX = states.imgWidth / wmfWidth;
            double scaleY = states.imgHeight / wmfHeight;
            states.scaling = (scaleX < scaleY) ? scaleX : scaleY;
        } else if (options->imgWidth > 0) {
            states.imgWidth = options->imgWidth;
            states.scaling = states.imgWidth / wmfWidth;
            states.imgHeight = wmfHeight * states.scaling;
        } else if (options->imgHeight > 0) {
            states.imgHeight = options->imgHeight;
            states.scaling = states.imgHeight / wmfHeight;
            states.imgWidth = wmfWidth * states.scaling;
        } else {
            /* Use DPI from placeable header */
            states.scaling = 96.0 / (double)Placeable.Inch;
            states.imgWidth = wmfWidth * states.scaling;
            states.imgHeight = wmfHeight * states.scaling;
        }
    } else {
        /* No placeable header - use defaults */
        states.windowExtX = 1000;
        states.windowExtY = 1000;
        states.scaling = 1.0;
        states.imgWidth = 1000;
        states.imgHeight = 1000;
    }

    states.viewportExtX = states.windowExtX;
    states.viewportExtY = states.windowExtY;
    states.mapMode = WMF_MM_ANISOTROPIC;

    /* Create output stream using fmem */
    fmem fm;
    fmem_init(&fm);
    FILE *stream = fmem_open(&fm, "w");
    if (!stream) {
        fmem_term(&fm);
        wmf_freeObjectTable(&states);
        wmf_freeDeviceContextStack(&states);
        wmf_freeDeviceContext(&states.currentDeviceContext);
        free(states.nameSpace);
        free(states.nameSpaceString);
        return -4;
    }

    /* Write SVG header */
    if (states.svgDelimiter) {
        fprintf(stream, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        fprintf(stream, "<%ssvg xmlns%s%s=\"http://www.w3.org/2000/svg\" ",
                states.nameSpaceString,
                strlen(states.nameSpace) > 0 ? ":" : "",
                states.nameSpace);
        fprintf(stream, "width=\"%.0f\" height=\"%.0f\" ",
                states.imgWidth, states.imgHeight);
        fprintf(stream, "viewBox=\"0 0 %.0f %.0f\">\n",
                states.imgWidth, states.imgHeight);
    }

    /* Process records */
    const char *recPtr = recStart;
    const char *endPtr = contents + length;
    int recNum = 0;

    while (recPtr < endPtr) {
        int recSize = wmf_onerec_draw(recPtr, endPtr, recNum, stream, &states);

        if (recSize <= 0) {
            /* EOF or error */
            break;
        }

        recPtr += recSize;
        recNum++;

        /* Safety check */
        if (recNum > 100000) {
            fprintf(stderr, "wmf2svg: Too many records, stopping\n");
            break;
        }
    }

    /* Write SVG footer */
    if (states.svgDelimiter) {
        fprintf(stream, "</%ssvg>\n", states.nameSpaceString);
    }

    /* Flush stream before getting memory buffer */
    fflush(stream);

    /* Get output from fmem */
    void *svg_buffer = NULL;
    size_t svg_size = 0;
    fmem_mem(&fm, &svg_buffer, &svg_size);

    /* Copy output to caller-owned memory */
    if (svg_size > 0) {
        *out = (char *)malloc(svg_size + 1);
        if (*out) {
            memcpy(*out, svg_buffer, svg_size);
            (*out)[svg_size] = '\0';
            *out_length = svg_size;
        }
    }

    /* Close stream and cleanup fmem */
    fclose(stream);
    fmem_term(&fm);
    wmf_freeObjectTable(&states);
    wmf_freeDeviceContextStack(&states);
    wmf_freeDeviceContext(&states.currentDeviceContext);
    free(states.nameSpace);
    free(states.nameSpaceString);

    return (*out != NULL) ? 0 : -5;
}
