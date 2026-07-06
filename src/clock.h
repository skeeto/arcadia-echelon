/*
 * clock.h — decentralized gameTime consensus (slew-to-fastest).
 *
 * Each client keeps gameTime = mono - epoch + slew, monotonic and never
 * rewound. On join it adopts the max (seed, gameTime) it hears; thereafter it
 * only ever speeds up (rate-capped) toward the running maximum, so the fastest
 * clock is a stable attractor. A light "anchor" (lowest present player id) is
 * used only to exempt its heartbeats from outlier rejection, guarding against a
 * single runaway clock. The game keeps running if the anchor drops.
 */
#ifndef ECHELON_CLOCK_H
#define ECHELON_CLOCK_H

#include <stdint.h>

typedef struct {
    uint32_t seed;
    uint32_t gen;           /* reset generation; higher wins over slew-to-max */
    int      have_game;     /* have we adopted/established a game yet? */
    uint32_t epoch_ms;      /* mono value we call gameTime 0           */
    int32_t  slew;          /* accumulated catch-up (ms)              */
    int      anchor_id;     /* lowest present player id               */
} Clock;

void     clock_init(Clock *c, uint32_t seed, uint32_t now_ms);
uint32_t clock_time(const Clock *c, uint32_t now_ms);
void     clock_set_anchor(Clock *c, int anchor_id);

/* Start a fresh game: new seed, gameTime back to 0, generation bumped so the
 * reset propagates to peers instead of being pulled back by slew-to-max. */
void clock_reset(Clock *c, uint32_t seed, uint32_t now_ms);

/* Fold a peer's heartbeat into our clock. Returns 1 if we adopted the peer's
 * game (seed changed) — the caller may want to react. */
int clock_observe(Clock *c, int my_id, int peer_id, uint32_t peer_seed,
                  uint32_t peer_gt, uint32_t peer_gen, int peer_is_anchor, uint32_t now_ms);

#endif /* ECHELON_CLOCK_H */
