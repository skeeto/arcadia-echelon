/*
 * glfont.h — crisp HUD text via wglUseFontBitmaps (OpenGL 1.1 + wgl).
 *
 * Turns a Windows font into GL bitmap display lists, so we get real text without
 * hand-rolling a stroke font. Call glfont_init once with the GL context current
 * and the font's DC; then glfont_text() draws at a raster position using the
 * current glColor, in whatever projection is active (set up a screen ortho).
 */
#ifndef ECHELON_GLFONT_H
#define ECHELON_GLFONT_H

#include <windows.h>

void glfont_init(HDC dc);
void glfont_destroy(void);
void glfont_text(float x, float y, const char *s);

#endif /* ECHELON_GLFONT_H */
