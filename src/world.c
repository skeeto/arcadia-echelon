/*
 * world.c — deterministic spawn schedule.
 *
 * For each integer spawn-tick T, independent PRNG streams decide whether an air
 * wave / ground enemy / obstacle / powerup spawns and with what parameters. The
 * live set for a frame is the union over the on-screen window of ticks. Screen
 * position is a function of the continuous scroll; existence and id are a
 * function of the integer T alone (the determinism guarantee).
 */
#include <math.h>
#include "world.h"
#include "prng.h"

/* Independent PRNG purposes so the spawn kinds don't correlate. */
enum { SALT_AIR = 1, SALT_GROUND = 2, SALT_OBST = 3, SALT_POWER = 4, SALT_HEAVY = 5 };

/* Base per-tick spawn probabilities (percent); several scale with difficulty. */
#define AIR_WAVE_PCT   7
#define GROUND_PCT     5
#define OBST_PCT       5
#define POWER_PCT      3
#define HEAVY_PCT_BASE 2

/* Independent sub-draw from a base draw (so we never over-shift one value). */
static uint64_t sub(uint64_t r, uint32_t k) { return sm64(r ^ (0x9E3779B97F4A7C15ull * (k + 1))); }

static float tick_world_y(int32_t T) { return (float)T * SPAWN_QUANTUM_WORLD; }
static float screen_y(float world_y, float scroll) { return (float)FIELD_H - (world_y - scroll); }

float world_scroll(uint32_t t) { return SCROLL_SPEED * ((float)t / 1000.0f); }

/* Difficulty grows ~1 per minute of gameTime; deterministic from the tick, so
 * every client ramps identically. */
static float difficulty(int32_t T) { return (float)T * (float)SPAWN_QUANTUM_MS / 60000.0f; }

static void gen_tick(uint32_t seed, int32_t T, float scroll, WorldView *wv)
{
    float wy = tick_world_y(T);

    /* ---- air wave: a formation flying a per-wave path (weave / strafe / dart) ---- */
    {
        uint64_t r = stream(seed, T, SALT_AIR);
        float d = difficulty(T);
        int wave_pct = AIR_WAVE_PCT + (int)(d * 3.0f);
        if (wave_pct > 26) wave_pct = 26;
        if (rng_chance(r, wave_pct)) {
            int   count = 2 + (int)rng_below(sub(r, 1), 3) + (int)(d * 0.4f);
            int   alt   = (int)rng_below(sub(r, 2), ALT_COUNT);
            int   path  = (int)rng_below(sub(r, 7), 3);       /* 0 weave 1 strafe 2 dart */
            float amp   = rng_range(sub(r, 3), 70.0f, 210.0f);
            float freq  = rng_range(sub(r, 4), 0.006f, 0.016f);
            float basex = rng_range(sub(r, 5), FIELD_W * 0.15f, FIELD_W * 0.85f);
            float phase = rng_unit(sub(r, 6)) * 6.2831853f;
            int   dir   = (sub(r, 8) & 1) ? 1 : -1;
            int   i;
            if (count > 7) count = 7;
            for (i = 0; i < count; i++) {
                float ey = screen_y(wy + i * 46.0f, scroll);
                float x;
                if (ey < -60.0f || ey > FIELD_H + 60.0f) continue;
                if (wv->n_enemies >= MAX_ENEMIES) break;
                if (path == 1)      x = basex + (float)dir * 0.55f * ey;             /* strafe */
                else if (path == 2) x = basex + amp * sinf(freq * 2.0f * ey + phase); /* dart  */
                else                x = basex + amp * sinf(freq * ey + phase);       /* weave */
                if (x < 40.0f) x = 40.0f;
                if (x > FIELD_W - 40.0f) x = FIELD_W - 40.0f;
                {
                    Enemy *e = &wv->enemies[wv->n_enemies++];
                    e->id       = MKID(IDK_ENEMY, T, i);
                    e->x        = x;
                    e->y        = ey;
                    e->r        = 15.0f;
                    e->kind     = EK_AIR;
                    e->altitude = (uint8_t)alt;
                    e->hpmax    = 1;
                    e->etype    = 0;
                    e->points   = 100;
                }
            }
        }
    }

    /* ---- heavy air gunship: tough, rare, more common as the run wears on ---- */
    {
        uint64_t r = stream(seed, T, SALT_HEAVY);
        float d = difficulty(T);
        int heavy_pct = HEAVY_PCT_BASE + (int)(d * 2.0f);
        if (heavy_pct > 14) heavy_pct = 14;
        if (rng_chance(r, heavy_pct)) {
            float ey = screen_y(wy, scroll);
            if (ey >= -60.0f && ey <= FIELD_H + 60.0f && wv->n_enemies < MAX_ENEMIES) {
                int   alt   = (int)rng_below(sub(r, 1), ALT_COUNT);
                float amp   = rng_range(sub(r, 2), 40.0f, 120.0f);
                float freq  = rng_range(sub(r, 3), 0.004f, 0.010f);
                float basex = rng_range(sub(r, 4), FIELD_W * 0.20f, FIELD_W * 0.80f);
                float phase = rng_unit(sub(r, 5)) * 6.2831853f;
                int   hp    = 4 + (int)d;
                float x     = basex + amp * sinf(freq * ey + phase);
                if (x < 50.0f) x = 50.0f;
                if (x > FIELD_W - 50.0f) x = FIELD_W - 50.0f;
                if (hp > 9) hp = 9;
                {
                    Enemy *e = &wv->enemies[wv->n_enemies++];
                    e->id       = MKID(IDK_ENEMY, T, 40);   /* slot 40: heavy lane */
                    e->x        = x;
                    e->y        = ey;
                    e->r        = 27.0f;
                    e->kind     = EK_AIR;
                    e->altitude = (uint8_t)alt;
                    e->hpmax    = (uint8_t)hp;
                    e->etype    = 1;
                    e->points   = 300;
                }
            }
        }
    }

    /* ---- ground enemy: static turret, tougher over time ---- */
    {
        uint64_t r = stream(seed, T, SALT_GROUND);
        if (rng_chance(r, GROUND_PCT)) {
            float ey = screen_y(wy, scroll);
            if (ey >= -40.0f && ey <= FIELD_H + 40.0f && wv->n_enemies < MAX_ENEMIES) {
                int hp = 3 + (int)(difficulty(T) * 0.5f);
                Enemy *e = &wv->enemies[wv->n_enemies++];
                if (hp > 7) hp = 7;
                e->id       = MKID(IDK_ENEMY, T, 32);   /* slot 32: ground lane   */
                e->x        = rng_range(sub(r, 1), FIELD_W * 0.10f, FIELD_W * 0.90f);
                e->y        = ey;
                e->r        = 20.0f;
                e->kind     = EK_GROUND;
                e->altitude = ALT_LOW;
                e->hpmax    = (uint8_t)hp;
                e->etype    = 2;
                e->points   = 150;
            }
        }
    }

    /* ---- obstacle: full-altitude column, clearable by bombs ---- */
    {
        uint64_t r = stream(seed, T, SALT_OBST);
        if (rng_chance(r, OBST_PCT)) {
            float ey = screen_y(wy, scroll);
            if (ey >= -90.0f && ey <= FIELD_H + 90.0f && wv->n_obstacles < MAX_OBSTACLES) {
                Obstacle *o = &wv->obstacles[wv->n_obstacles++];
                o->id      = MKID(IDK_OBSTACLE, T, 0);
                o->x       = rng_range(sub(r, 1), FIELD_W * 0.15f, FIELD_W * 0.85f);
                o->y       = ey;
                o->w       = rng_range(sub(r, 2), 60.0f, 130.0f);
                o->h       = 74.0f;
                o->top_alt = ALT_HIGH;
                o->hpmax   = 3;
                o->kind    = (uint8_t)rng_below(sub(r, 3), 3);
            }
        }
    }

    /* ---- powerup: schedule-placed (predetermined by seed), reads as a drop ---- */
    {
        uint64_t r = stream(seed, T, SALT_POWER);
        if (rng_chance(r, POWER_PCT)) {
            float ey = screen_y(wy, scroll);
            if (ey >= -40.0f && ey <= FIELD_H + 40.0f && wv->n_powerups < MAX_POWERUPS) {
                uint32_t k = rng_below(sub(r, 1), 100);
                int kind = (k < 8)  ? PU_1UP   : (k < 20) ? PU_WEAPON :
                           (k < 45) ? PU_POWER : (k < 72) ? PU_WIDTH  : PU_SPECIAL;
                Powerup *p = &wv->powerups[wv->n_powerups++];
                p->id   = MKID(IDK_POWERUP, T, 0);
                p->x    = rng_range(sub(r, 2), FIELD_W * 0.15f, FIELD_W * 0.85f);
                p->y    = ey;
                p->r    = 14.0f;
                p->kind = (uint8_t)kind;
            }
        }
    }
}

void world_build(uint32_t seed, uint32_t game_time_ms, WorldView *wv)
{
    float   scroll = world_scroll(game_time_ms);
    int32_t Tc     = (int32_t)(game_time_ms / SPAWN_QUANTUM_MS);
    int32_t span   = (int32_t)((float)FIELD_H / SPAWN_QUANTUM_WORLD) + 1;
    int32_t Tlo    = Tc - GEN_MARGIN_TICKS;
    int32_t Thi    = Tc + span + GEN_MARGIN_TICKS;
    int32_t T;

    wv->seed = seed;
    wv->scroll = scroll;
    wv->T = Tc;
    wv->n_enemies = wv->n_obstacles = wv->n_powerups = 0;

    for (T = Tlo; T <= Thi; T++)
        if (T >= 0) gen_tick(seed, T, scroll, wv);
}
