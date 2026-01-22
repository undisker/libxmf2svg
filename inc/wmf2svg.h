/**
 * @file wmf2svg.h
 * @brief WMF (Windows Metafile) to SVG conversion library
 *
 * This library provides functions to convert WMF files to SVG format.
 * Based on libuemf for WMF parsing.
 */

#ifndef WMF2SVG_H
#define WMF2SVG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Options structure for WMF to SVG conversion
 */
typedef struct {
    /** SVG namespace prefix (the '<something>:' before each element) */
    char *nameSpace;
    /** Verbose mode - output fields and values if true */
    bool verbose;
    /** Draw SVG document delimiter tags or not */
    bool svgDelimiter;
    /** Target image height in pixels (0 = use original) */
    double imgHeight;
    /** Target image width in pixels (0 = use original) */
    double imgWidth;
} wmfGeneratorOptions;

/**
 * @brief Convert WMF content to SVG
 *
 * @param contents Pointer to the WMF file contents in memory
 * @param length Size of the WMF content in bytes
 * @param out Pointer to receive the allocated SVG output string
 * @param out_length Pointer to receive the length of the SVG output
 * @param options Conversion options
 * @return 0 on success, non-zero on error
 */
#ifdef _MSC_VER
__declspec(dllexport)
#endif
int wmf2svg(char *contents, size_t length, char **out, size_t *out_length,
            wmfGeneratorOptions *options);

/**
 * @brief Check if a file is a valid WMF file
 *
 * @param contents Pointer to the file contents
 * @param length Size of the content in bytes
 * @param is_wmf Pointer to receive the result (true if valid WMF)
 * @return 0 on success, non-zero on error
 */
#ifdef _MSC_VER
__declspec(dllexport)
#endif
int wmf2svg_is_wmf(char *contents, size_t length, bool *is_wmf);

#ifdef __cplusplus
}
#endif

#endif /* WMF2SVG_H */
