/*
 * render.h — OpenGL rendering on a dedicated thread.
 *
 * Owns the GL child window over the Arcadia canvas and its render thread (the
 * glcube pattern). Reads the latest RenderSnapshot, rebuilds the deterministic
 * world at an extrapolated time for smooth scroll/enemy motion, and draws the
 * top-down altitude+shadow view plus HUD. Never mutates game state.
 */
#ifndef ECHELON_RENDER_H
#define ECHELON_RENDER_H

#include <windows.h>

void render_start(HWND canvas);   /* call from open() on the host thread */
void render_stop(void);           /* call from close() */

#endif /* ECHELON_RENDER_H */
