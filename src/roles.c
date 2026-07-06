#include <math.h>
#include "roles.h"
#include "config.h"

/* One weapon's base behavior; width/power scale count and damage at fire time. */
typedef struct {
    const char *name;
    int   base_count;    /* projectiles at width 0                               */
    float spread_deg;    /* total fan angle across the projectiles               */
    int   base_dmg;
    int   is_bomb;       /* ground attack                                         */
    int   pierce;
    int   cooldown_ms;
    int   width_extra;   /* projectiles added per width level                     */
    int   power_dmg;     /* damage added per power level                          */
    int   mixed;         /* multi-role "split": half air, half bomb              */
    int   homing;        /* air bolts steer toward the nearest air enemy (F4)     */
} WSpec;

/* [role][weapon] — F1..F3 core, F4 (index 3) unlocked by a powerup. */
static const WSpec WEAPONS[ROLE_COUNT][4] = {
    /* FIGHTER — air only */
    {
        { "PULSE",  1, 0.0f,  1, 0, 0, 140, 1, 1, 0, 0 },
        { "SPREAD", 3, 44.0f, 1, 0, 0, 210, 2, 1, 0, 0 },
        { "LANCE",  1, 0.0f,  2, 0, 1, 240, 1, 2, 0, 0 },
        { "HOMING", 1, 0.0f,  1, 0, 0, 200, 1, 1, 0, 1 },
    },
    /* BOMBER — ground only */
    {
        { "DROP",   1, 0.0f,  1, 1, 0, 220, 1, 1, 0, 0 },
        { "CLUSTER",2, 60.0f, 1, 1, 0, 320, 2, 1, 0, 0 },
        { "PENETR", 1, 0.0f,  3, 1, 1, 380, 1, 2, 0, 0 },
        { "CARPET", 3, 90.0f, 1, 1, 0, 420, 1, 1, 0, 0 },
    },
    /* MULTI — both, weaker */
    {
        { "BLAST",  1, 0.0f,  1, 0, 0, 175, 1, 1, 0, 0 },
        { "SPLIT",  2, 30.0f, 1, 0, 0, 260, 1, 1, 1, 0 },
        { "BLAST",  1, 0.0f,  1, 0, 0, 175, 1, 1, 0, 0 },  /* alias */
        { "HOMING", 1, 0.0f,  1, 0, 0, 240, 1, 1, 0, 1 },
    },
};

static const char *ROLE_NAMES[ROLE_COUNT] = { "FIGHTER", "BOMBER", "MULTI" };

const char *role_name(int role)
{
    return (role >= 0 && role < ROLE_COUNT) ? ROLE_NAMES[role] : "?";
}

int role_num_weapons(int role)
{
    return (role == ROLE_MULTI) ? 2 : 3;   /* multi exposes F1..F2 */
}

const char *weapon_name(int role, int weapon)
{
    if (role < 0 || role >= ROLE_COUNT) return "?";
    if (weapon < 0) weapon = 0;
    if (weapon > 3) weapon = 3;
    return WEAPONS[role][weapon].name;
}

static void add_proj(Proj *out, int cap, int *n, float x, float y,
                     float ang_deg, float speed, int alt, int is_bomb,
                     int pierce, int dmg, int owner, float radius, int homing)
{
    float a;
    if (*n >= cap) return;
    a = ang_deg * 3.14159265f / 180.0f;
    out[*n].x        = x;
    out[*n].y        = y;
    out[*n].vx       = speed * sinf(a);
    out[*n].vy       = -speed * cosf(a);   /* upward (negative y) */
    out[*n].altitude = (uint8_t)alt;
    out[*n].is_bomb  = (uint8_t)is_bomb;
    out[*n].active   = 1;
    out[*n].pierce   = (uint8_t)pierce;
    out[*n].dmg      = dmg;
    out[*n].owner    = owner;
    out[*n].fall     = is_bomb ? 1.0f : 0.0f;
    out[*n].fuse     = is_bomb ? (float)BOMB_FALL_MS : 0.0f;
    out[*n].radius   = radius;
    out[*n].cosmetic = 0;
    out[*n].homing   = (uint8_t)homing;
    (*n)++;
}

int role_fire(int role, int weapon, int width, int power,
              float x, float y, int alt, int owner,
              Proj *out, int cap, int *out_count)
{
    const WSpec *w;
    int count, dmg, i;
    float step, start;

    *out_count = 0;
    if (role < 0 || role >= ROLE_COUNT) return 0;
    if (weapon < 0) weapon = 0;
    if (weapon > 3) weapon = 3;    /* index 3 = F4 unlock; sim gates selection */
    w = &WEAPONS[role][weapon];

    count = w->base_count + w->width_extra * width;
    if (count < 1) count = 1;
    dmg   = w->base_dmg + w->power_dmg * power;

    /* Fan the projectiles evenly across spread_deg, centered on straight-up. */
    step  = (count > 1) ? (w->spread_deg / (count - 1)) : 0.0f;
    start = -0.5f * w->spread_deg;

    if (w->mixed) {
        /* SPLIT: one air bolt at the player's band + one ground bomb. */
        add_proj(out, cap, out_count, x, y, -8.0f, PROJ_SPEED, alt, 0, 0, dmg, owner, 0.0f, 0);
        add_proj(out, cap, out_count, x, y,  8.0f, BOMB_FWD,   alt, 1, 0, dmg, owner, BOMB_RADIUS, 0);
        return w->cooldown_ms;
    }

    for (i = 0; i < count; i++) {
        float ang = start + step * i;
        float spd = w->is_bomb ? BOMB_FWD : PROJ_SPEED;
        float rad = w->is_bomb ? BOMB_RADIUS : 0.0f;
        /* bombs carry the launch altitude for the render arc; targeting is ground AoE */
        add_proj(out, cap, out_count, x, y, ang, spd, alt, w->is_bomb, w->pierce, dmg, owner, rad, w->homing);
    }
    return w->cooldown_ms;
}

int role_special(int role, int width, int power, float x, float y, int alt,
                 int owner, Proj *out, int cap, int *out_count)
{
    (void)width;
    *out_count = 0;

    if (role == ROLE_BOMBER) {
        /* Terrain-clear: one heavy, wide-radius bomb that opens a corridor. */
        int dmg = BOMBER_SPECIAL_DMG + power;
        add_proj(out, cap, out_count, x, y, 0.0f, BOMB_FWD, alt, 1, 0, dmg, owner, BOMBER_SPECIAL_RADIUS, 0);
        return SPECIAL_COOLDOWN_MS;
    }

    /* Fighter (and a weaker multi): a wide air burst that sweeps the band. */
    {
        int   n      = (role == ROLE_MULTI) ? (FIGHTER_SPECIAL_BOLTS - 5) : FIGHTER_SPECIAL_BOLTS;
        int   dmg    = 1 + power;
        float spread = (role == ROLE_MULTI) ? 90.0f : FIGHTER_SPECIAL_SPREAD;
        float step, startA;
        int   i;
        if (n < 1) n = 1;
        step   = (n > 1) ? spread / (n - 1) : 0.0f;
        startA = -0.5f * spread;
        for (i = 0; i < n; i++)
            add_proj(out, cap, out_count, x, y, startA + step * i, PROJ_SPEED, alt, 0, 0, dmg, owner, 0.0f, 0);
        return SPECIAL_COOLDOWN_MS;
    }
}
