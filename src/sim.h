/*
 * sim.h — the mutable game (host thread only).
 *
 * Owns the local player, projectiles, dead-id set, and score/lives. Steps from
 * tick() with real elapsed time. The deterministic world is rebuilt each step
 * from (seed, gameTime); nothing about spawns is stored here — only which ids
 * have been killed/cleared/taken (so we skip them).
 */
#ifndef ECHELON_SIM_H
#define ECHELON_SIM_H

#include "types.h"
#include "snapshot.h"
#include "clock.h"
#include "net.h"

/* Per-frame input, gathered by echelon.c from ar_key_down (host thread). */
typedef struct {
    int left, right, up, down;   /* held */
    int fire;                    /* held */
    int special;                 /* held; role special (F5) */
    int ascend, descend;         /* held; sim debounces by time */
    int weapon;                  /* 0 none, else 1..N selects a weapon (F-keys) */
    int role_sel;                /* 0 none, else 1..3 selects a role */
    int restart;                 /* rejoin after game over */
} Input;

typedef struct {
    float    x, y;
    uint8_t  altitude;        /* target band set by Home/End */
    float    alt_vis;         /* eased visual/effective height (bands) */
    uint8_t  eff_alt;         /* round(alt_vis): the band gameplay uses */
    uint8_t  role, weapon, width_lvl, power_lvl;
    uint8_t  special_charges;
    uint8_t  weapon4;        /* F4 slot unlocked by a PU_WEAPON pickup */
    int32_t  score;
    int      lives;
    uint8_t  alive;
    uint32_t respawn_at, invuln_until, last_fire, last_alt, last_special;
} LocalPlayer;

/* Sound cues the host thread should play this tick (see SND_FILE in echelon.c). */
enum { SND_SHOOT = 0, SND_EXPLODE, SND_POWERUP, SND_DEATH, SND_ONEUP, SND_BOMB, SND_COUNT };

/* Outgoing co-op events the host thread should broadcast this tick. */
typedef struct { uint8_t kind; uint32_t id; int32_t points; } OutEvent;
typedef struct { int16_t x, y; uint8_t alt; } OutFire;

/* Accumulated damage against a multi-hit entity id, aged with the scroll. */
typedef struct { uint32_t id; int dmg; } HitAcc;

/* An active bomb burst, for the render thread to animate. */
typedef struct { float x, y; uint32_t start; } Expl;

/* A death burst where a shot destroyed an enemy (renders as flying fragments). */
typedef struct { float x, y; uint8_t alt, etype; uint32_t start, seed; } Spark;

/* A deterministic turret bullet (regenerated each tick; no persistent state). */
typedef struct { float x, y; } TBullet;

/* A remote player, updated from PLAYER_STATE messages. vx/vy are derived from
 * consecutive updates; dx/dy is the smoothed display position that eases toward
 * a (capped) prediction each tick, so 10 Hz updates render without snapping. */
typedef struct {
    int32_t  id;
    char     name[28];
    float    x, y, vx, vy;    /* last authoritative sample + derived velocity */
    float    dx, dy;          /* smoothed display position */
    float    alt_vis;         /* eased altitude (bands) */
    uint8_t  alt, role, weapon, width, power, alive;
    int32_t  score, best;
    uint32_t last_seen;
} Remote;

typedef struct {
    uint32_t    seed;
    Clock       clock;
    int         local_id;
    char        local_name[28];
    uint32_t    game_time_ms, last_ms;
    LocalPlayer p;
    Remote      remotes[MAX_PLAYERS]; int n_remotes;
    Proj        proj[MAX_PROJECTILES]; int n_proj;
    WorldView   wv;
    uint32_t    dead[MAX_DEAD_IDS]; int n_dead;   /* sorted ascending */
    HitAcc      hit[96]; int n_hit;
    Expl        expl[MAX_EXPL]; int n_expl;
    Spark       spark[MAX_SPARK]; int n_spark;
    TBullet     tbul[MAX_TBUL]; int n_tbul;
    OutEvent    oevents[32]; int n_oevents;       /* to broadcast this tick */
    OutFire     ofires[8];   int n_ofires;
    uint8_t     osounds[16]; int n_osounds;       /* sound cues this tick */
    int32_t     hi_score;
    uint8_t     gameover;
} GameState;

void sim_init(GameState *g, uint32_t seed, uint32_t now_ms);
void sim_advance(GameState *g, const Input *in, uint32_t now_ms);
void sim_snapshot(GameState *g, RenderSnapshot *s, uint32_t now_ms);

/* Host-thread network hooks. */
void sim_apply_state(GameState *g, const MsgState *m, uint32_t now_ms);
void sim_apply_event(GameState *g, const MsgEvent *m);
void sim_apply_fire(GameState *g, const MsgFire *m, uint32_t now_ms);
void sim_set_name(GameState *g, int id, const char *name);

#endif /* ECHELON_SIM_H */
