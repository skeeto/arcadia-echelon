/*
 * types.h — shared entity + world structs and the deterministic id scheme.
 *
 * Field/screen coordinates: x in [0,FIELD_W], y in [0,FIELD_H] with y growing
 * DOWNWARD (0 = top of the field, FIELD_H = bottom, where the player sits). The
 * world scrolls so entities enter at the top and move down toward the player.
 */
#ifndef ECHELON_TYPES_H
#define ECHELON_TYPES_H

#include <stdint.h>
#include "config.h"

/* ---- deterministic entity ids: (kind, spawn-tick, slot) packed into 32 bits.
 * Any client decodes the same entity from an id in a KILL/CLEARED/TAKEN msg. */
enum { IDK_ENEMY = 1, IDK_OBSTACLE = 2, IDK_POWERUP = 3 };
#define MKID(kind, tick, slot) \
    (((uint32_t)(kind) << 29) | (((uint32_t)(tick) & 0x7FFFFF) << 6) | ((uint32_t)(slot) & 0x3F))
#define ID_KIND(id) ((uint32_t)(id) >> 29)

enum { EK_AIR = 0, EK_GROUND = 1 };
enum { PU_WIDTH = 0, PU_POWER = 1, PU_1UP = 2, PU_SPECIAL = 3, PU_WEAPON = 4, PU_COUNT = 5 };

typedef struct {
    uint32_t id;
    float    x, y, r;     /* field coords + collision radius (computed each build) */
    uint8_t  kind;        /* EK_AIR / EK_GROUND                                    */
    uint8_t  altitude;    /* band it occupies (air); ground = ALT_LOW              */
    uint8_t  hpmax;       /* hits to destroy                                       */
    uint8_t  etype;       /* render archetype: 0 grunt, 1 heavy, 2 turret          */
    int32_t  points;
} Enemy;

typedef struct {
    uint32_t id;
    float    x, y, w, h;  /* field coords + footprint                              */
    uint8_t  top_alt;     /* blocks bands 0..top_alt (ALT_HIGH = full column)      */
    uint8_t  hpmax;       /* bomb hits to clear (before bomber power scaling)       */
    uint8_t  kind;        /* rock/tree/tower -> color+shape                        */
} Obstacle;

typedef struct {
    uint32_t id;
    float    x, y, r;
    uint8_t  kind;        /* PU_*                                                  */
} Powerup;

/* A live projectile (mutable, owned by the sim). Air bolts hit air enemies in
 * their altitude band; bombs (is_bomb) hit ground enemies + obstacles. */
typedef struct {
    float    x, y, vx, vy;
    uint8_t  altitude;    /* air bolt: target band; bomb: launch band (render ref)  */
    uint8_t  is_bomb;
    uint8_t  active;
    uint8_t  pierce;      /* survives a hit (lance/penetrator)                     */
    int32_t  dmg;
    int      owner;       /* player id (for multiplayer attribution)               */
    float    fall;        /* bomb: 1 at launch altitude -> 0 at the ground         */
    float    fuse;        /* bomb: ms remaining until it bursts                    */
    float    radius;      /* bomb: area-of-effect burst radius                     */
    uint8_t  cosmetic;    /* a remote's shot, echoed for show — no collision       */
    uint8_t  homing;      /* air bolt that steers toward the nearest air enemy      */
} Proj;

/* The live world at one instant, a pure function of (seed, gameTime). Rebuilt
 * fresh every frame; mutable "dead/cleared/taken" state lives in the sim, not
 * here, and is applied by skipping ids. */
typedef struct {
    uint32_t seed;
    float    scroll;
    int32_t  T;                              /* current spawn-tick                */
    Enemy    enemies[MAX_ENEMIES];     int n_enemies;
    Obstacle obstacles[MAX_OBSTACLES]; int n_obstacles;
    Powerup  powerups[MAX_POWERUPS];   int n_powerups;
} WorldView;

#endif /* ECHELON_TYPES_H */
