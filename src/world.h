/*
 * world.h — deterministic world generator.
 *
 * world_build() fills a caller-owned WorldView with every live entity for the
 * given gameTime. It is a pure function of (seed, gameTime): no globals, no
 * mutable state, safe to call from any thread. The host-thread sim and the
 * render thread each call it with their own gameTime and get self-consistent
 * results; because entity existence/ids derive from the integer spawn-tick,
 * all clients agree on what exists and what its id is.
 */
#ifndef ECHELON_WORLD_H
#define ECHELON_WORLD_H

#include "types.h"

void  world_build(uint32_t seed, uint32_t game_time_ms, WorldView *wv);
float world_scroll(uint32_t game_time_ms);   /* SCROLL_SPEED * t, in world units */

#endif /* ECHELON_WORLD_H */
