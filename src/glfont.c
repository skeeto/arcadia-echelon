#include <string.h>
#include <GL/gl.h>
#include "glfont.h"

static GLuint s_base;    /* first of 96 display lists (ASCII 32..127) */
static HFONT  s_font;

void glfont_init(HDC dc)
{
    HGDIOBJ old;
    if (s_base) return;
    s_font = CreateFontA(-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                         ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Verdana");
    if (!s_font) return;
    old = SelectObject(dc, s_font);
    s_base = glGenLists(96);
    if (s_base) wglUseFontBitmaps(dc, 32, 96, s_base);
    SelectObject(dc, old);
}

void glfont_destroy(void)
{
    if (s_base) { glDeleteLists(s_base, 96); s_base = 0; }
    if (s_font) { DeleteObject(s_font); s_font = 0; }
}

void glfont_text(float x, float y, const char *s)
{
    if (!s_base || !s || !*s) return;
    glRasterPos2f(x, y);
    glPushAttrib(GL_LIST_BIT);
    glListBase(s_base - 32);
    glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, (const GLvoid *)s);
    glPopAttrib();
}
