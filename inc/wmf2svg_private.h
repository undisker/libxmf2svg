/**
 * @file wmf2svg_private.h
 * @brief Internal structures and functions for WMF to SVG conversion
 */

#ifndef WMF2SVG_PRIVATE_H
#define WMF2SVG_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "uwmf.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* Color output macros for verbose mode */
#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"

#define wmf_verbose_printf(...)                                                \
    if (states->verbose)                                                       \
        printf(__VA_ARGS__);

#define WMF_FLAG_SUPPORTED                                                     \
    wmf_verbose_printf("   Status:         %sSUPPORTED%s\n", KGRN, KNRM);
#define WMF_FLAG_IGNORED                                                       \
    wmf_verbose_printf("   Status:         %sIGNORED%s\n", KRED, KNRM);
#define WMF_FLAG_PARTIAL                                                       \
    wmf_verbose_printf("   Status:         %sPARTIAL SUPPORT%s\n", KYEL, KNRM);

#define UNUSED(x) (void)(x)

#define mmPerInch 25.4

/* Drawing modes */
#define DRAW_PAINT 0
#define DRAW_PATTERN 1
#define DRAW_IMAGE 2

/* Map modes */
#define WMF_MM_TEXT        1
#define WMF_MM_LOMETRIC    2
#define WMF_MM_HIMETRIC    3
#define WMF_MM_LOENGLISH   4
#define WMF_MM_HIENGLISH   5
#define WMF_MM_TWIPS       6
#define WMF_MM_ISOTROPIC   7
#define WMF_MM_ANISOTROPIC 8

/* Polygon fill modes */
#define WMF_ALTERNATE 1
#define WMF_WINDING   2

/* Background modes */
#define WMF_TRANSPARENT 1
#define WMF_OPAQUE      2

/* Brush styles */
#define WMF_BS_SOLID         0
#define WMF_BS_NULL          1
#define WMF_BS_HOLLOW        1
#define WMF_BS_HATCHED       2
#define WMF_BS_PATTERN       3
#define WMF_BS_INDEXED       4
#define WMF_BS_DIBPATTERN    5
#define WMF_BS_DIBPATTERNPT  6
#define WMF_BS_PATTERN8X8    7
#define WMF_BS_DIBPATTERN8X8 8

/* Pen styles */
#define WMF_PS_SOLID       0
#define WMF_PS_DASH        1
#define WMF_PS_DOT         2
#define WMF_PS_DASHDOT     3
#define WMF_PS_DASHDOTDOT  4
#define WMF_PS_NULL        5
#define WMF_PS_INSIDEFRAME 6

/* Hatch styles */
#define WMF_HS_HORIZONTAL  0
#define WMF_HS_VERTICAL    1
#define WMF_HS_FDIAGONAL   2
#define WMF_HS_BDIAGONAL   3
#define WMF_HS_CROSS       4
#define WMF_HS_DIAGCROSS   5

/* Text alignment */
#define WMF_TA_NOUPDATECP  0x0000
#define WMF_TA_UPDATECP    0x0001
#define WMF_TA_LEFT        0x0000
#define WMF_TA_RIGHT       0x0002
#define WMF_TA_CENTER      0x0006
#define WMF_TA_TOP         0x0000
#define WMF_TA_BOTTOM      0x0008
#define WMF_TA_BASELINE    0x0018

/* Stock objects */
#define WMF_WHITE_BRUSH         0x80000000
#define WMF_LTGRAY_BRUSH        0x80000001
#define WMF_GRAY_BRUSH          0x80000002
#define WMF_DKGRAY_BRUSH        0x80000003
#define WMF_BLACK_BRUSH         0x80000004
#define WMF_NULL_BRUSH          0x80000005
#define WMF_WHITE_PEN           0x80000006
#define WMF_BLACK_PEN           0x80000007
#define WMF_NULL_PEN            0x80000008
#define WMF_OEM_FIXED_FONT      0x8000000A
#define WMF_ANSI_FIXED_FONT     0x8000000B
#define WMF_ANSI_VAR_FONT       0x8000000C
#define WMF_SYSTEM_FONT         0x8000000D
#define WMF_DEVICE_DEFAULT_FONT 0x8000000E
#define WMF_DEFAULT_PALETTE     0x8000000F
#define WMF_SYSTEM_FIXED_FONT   0x80000010

/* Object types in the object table */
#define WMF_OBJ_INVALID 0
#define WMF_OBJ_PEN     1
#define WMF_OBJ_BRUSH   2
#define WMF_OBJ_FONT    3
#define WMF_OBJ_PALETTE 4
#define WMF_OBJ_REGION  5

/**
 * @brief Point with double coordinates
 */
typedef struct {
    double x;
    double y;
} WMF_POINT_D;

/**
 * @brief Graphics object in the object table
 */
typedef struct wmf_graph_object {
    int type; /* WMF_OBJ_* */

    /* Font properties */
    bool font_set;
    char *font_name;
    int16_t font_height;
    int16_t font_width;
    int16_t font_escapement;
    int16_t font_orientation;
    int16_t font_weight;
    uint8_t font_italic;
    uint8_t font_underline;
    uint8_t font_strikeout;
    uint8_t font_charset;

    /* Pen (stroke) properties */
    bool stroke_set;
    uint16_t stroke_style;
    uint8_t stroke_red;
    uint8_t stroke_green;
    uint8_t stroke_blue;
    double stroke_width;

    /* Brush (fill) properties */
    bool fill_set;
    uint16_t fill_style;
    uint16_t fill_hatch;
    uint8_t fill_red;
    uint8_t fill_green;
    uint8_t fill_blue;

} WMF_GRAPH_OBJECT;

/**
 * @brief WMF Device Context structure
 */
typedef struct wmf_device_context {
    /* Font properties */
    bool font_set;
    char *font_name;
    int16_t font_height;
    int16_t font_width;
    int16_t font_escapement;
    int16_t font_orientation;
    int16_t font_weight;
    uint8_t font_italic;
    uint8_t font_underline;
    uint8_t font_strikeout;
    uint8_t font_charset;

    /* Pen (stroke) properties */
    bool stroke_set;
    uint16_t stroke_style;
    uint8_t stroke_red;
    uint8_t stroke_green;
    uint8_t stroke_blue;
    double stroke_width;

    /* Brush (fill) properties */
    bool fill_set;
    uint16_t fill_style;
    uint16_t fill_hatch;
    uint8_t fill_red;
    uint8_t fill_green;
    uint8_t fill_blue;

    /* Fill mode for polygons */
    uint16_t fill_polymode;

    /* Text properties */
    uint8_t text_red;
    uint8_t text_green;
    uint8_t text_blue;
    uint16_t text_align;

    /* Background properties */
    uint8_t bk_red;
    uint8_t bk_green;
    uint8_t bk_blue;
    uint16_t bk_mode;

    /* ROP2 mode */
    uint16_t rop2_mode;

} WMF_DEVICE_CONTEXT;

/**
 * @brief Stack of WMF Device Contexts
 */
typedef struct wmf_dc_stack {
    WMF_DEVICE_CONTEXT DeviceContext;
    struct wmf_dc_stack *previous;
} WMF_DEVICE_CONTEXT_STACK;

/**
 * @brief Main drawing states structure
 */
typedef struct wmf_drawing_states {
    /* Unique ID counter */
    int uniqId;

    /* SVG namespace */
    char *nameSpace;
    char *nameSpaceString;

    /* Verbose mode */
    bool verbose;

    /* Draw SVG delimiters */
    bool svgDelimiter;

    /* Error flag */
    bool Error;

    /* End address of WMF content for bounds checking */
    uint64_t endAddress;

    /* Current device context */
    WMF_DEVICE_CONTEXT currentDeviceContext;

    /* Device context stack */
    WMF_DEVICE_CONTEXT_STACK *DeviceContextStack;

    /* Object table */
    WMF_GRAPH_OBJECT *objectTable;
    int objectTableSize;

    /* Coordinate transformation */
    double scaling;
    int16_t windowOrgX;
    int16_t windowOrgY;
    int16_t windowExtX;
    int16_t windowExtY;
    int16_t viewportOrgX;
    int16_t viewportOrgY;
    int16_t viewportExtX;
    int16_t viewportExtY;
    uint16_t mapMode;

    /* Placeable header info */
    bool hasPlaceable;
    U_RECT16 placeableBounds;
    uint16_t placeableInch;

    /* Image dimensions */
    double imgHeight;
    double imgWidth;
    double pxPerMm;

    /* Current cursor position */
    double cur_x;
    double cur_y;

} WMF_DRAWING_STATES;

/* Buffer size for string operations */
#define WMF_BUFFERSIZE 1024

/* Function prototypes */

/* Device context management */
void wmf_saveDeviceContext(WMF_DRAWING_STATES *states);
void wmf_copyDeviceContext(WMF_DEVICE_CONTEXT *dest, WMF_DEVICE_CONTEXT *src);
void wmf_restoreDeviceContext(WMF_DRAWING_STATES *states, int16_t index);
void wmf_freeDeviceContextStack(WMF_DRAWING_STATES *states);
void wmf_freeDeviceContext(WMF_DEVICE_CONTEXT *dc);

/* Object table management */
void wmf_freeObjectTable(WMF_DRAWING_STATES *states);
int wmf_createObject(WMF_DRAWING_STATES *states, WMF_GRAPH_OBJECT *obj);
void wmf_deleteObject(WMF_DRAWING_STATES *states, uint16_t index);
void wmf_selectObject(WMF_DRAWING_STATES *states, uint16_t index);

/* Coordinate transformation */
double wmf_scaleX(WMF_DRAWING_STATES *states, int16_t x);
double wmf_scaleY(WMF_DRAWING_STATES *states, int16_t y);
WMF_POINT_D wmf_point_scale(WMF_DRAWING_STATES *states, int16_t x, int16_t y);

/* Drawing helpers */
void wmf_stroke_style(WMF_DRAWING_STATES *states, FILE *out);
void wmf_fill_style(WMF_DRAWING_STATES *states, FILE *out);
int wmf_get_id(WMF_DRAWING_STATES *states);

/* Bounds checking */
bool wmf_checkOutOfBounds(WMF_DRAWING_STATES *states, uintptr_t address);

/* Record processing */
int wmf_onerec_draw(const char *contents, const char *blimit, int recnum,
                    FILE *out, WMF_DRAWING_STATES *states);

/* Utility */
char *wmf_base64_encode(const unsigned char *data, size_t input_length,
                        size_t *output_length);

#ifdef __cplusplus
}
#endif

#endif /* WMF2SVG_PRIVATE_H */
