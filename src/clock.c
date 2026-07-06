#include "clock.h"
#include "config.h"

void clock_init(Clock *c, uint32_t seed, uint32_t now_ms)
{
    c->seed = seed;
    c->have_game = 1;          /* we assume we're first until we hear otherwise */
    c->epoch_ms = now_ms;      /* gameTime starts at 0 */
    c->slew = 0;
    c->anchor_id = -1;
}

uint32_t clock_time(const Clock *c, uint32_t now_ms)
{
    return now_ms - c->epoch_ms + (uint32_t)c->slew;
}

void clock_set_anchor(Clock *c, int anchor_id)
{
    c->anchor_id = anchor_id;
}

/* Re-base so that clock_time(now) == gt exactly (used on adoption). */
static void adopt_time(Clock *c, uint32_t gt, uint32_t now_ms)
{
    c->epoch_ms = now_ms - gt;
    c->slew = 0;
}

int clock_observe(Clock *c, int my_id, int peer_id, uint32_t peer_seed,
                  uint32_t peer_gt, int peer_is_anchor, uint32_t now_ms)
{
    uint32_t my_gt = clock_time(c, now_ms);
    int32_t  ahead = (int32_t)(peer_gt - my_gt);   /* >0: peer is ahead of us */

    if (peer_seed != c->seed) {
        /* Different game instances — converge on one. The more-established
         * (higher gameTime) wins; on a near tie the lower player id wins, so
         * both sides pick the same seed deterministically. */
        if (ahead > 300 || ((ahead > -300) && peer_id < my_id)) {
            c->seed = peer_seed;
            adopt_time(c, peer_gt, now_ms);
            return 1;
        }
        return 0;   /* keep ours; the peer will adopt us */
    }

    /* Same game: slew-to-max. Never rewind; only speed up if behind. */
    if (ahead > MAX_PLAUSIBLE_LEAD) {
        if (!peer_is_anchor) return 0;          /* reject runaway non-anchor */
        ahead = MAX_PLAUSIBLE_LEAD;             /* anchor may still pull us */
    }
    if (ahead > 0) {
        int32_t step = ahead < CATCHUP_STEP ? ahead : CATCHUP_STEP;
        c->slew += step;
    }
    return 0;
}
