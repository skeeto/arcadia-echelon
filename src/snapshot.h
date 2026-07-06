/*
 * snapshot.h — the one concurrency seam.
 *
 * The sim (host thread) publishes an immutable RenderSnapshot; the render thread
 * consumes the latest copy. A single CRITICAL_SECTION guards the hand-off; the
 * struct is plain POD so the copy under lock is a fast memcpy.
 */
#ifndef ECHELON_SNAPSHOT_H
#define ECHELON_SNAPSHOT_H

#include <stdint.h>
#include "config.h"

typedef struct {
    float   x, y, vy;
    float   fall;             /* bomb: 1 at launch band -> 0 at the ground */
    uint8_t altitude, is_bomb, active;
} SnapProj;

typedef struct {
    float x, y, progress;     /* 0..1 through the burst animation */
} SnapExpl;

typedef struct {
    float    x, y, progress;
    uint32_t seed;
    uint8_t  alt, etype;
} SnapSpark;

typedef struct { float x, y; } SnapTBul;

typedef struct {
    float   x, y;             /* already-smoothed display position */
    float   alt_f;            /* eased altitude (bands) */
    uint8_t role, alive;
} SnapRemote;

typedef struct {
    char    name[16];
    int32_t best;
    uint8_t is_local;
} SnapScore;

typedef struct {
    uint32_t seed;
    uint32_t game_time_ms;    /* sim gameTime at publish */
    uint32_t stamp_ms;        /* host GetTickCount at publish (for extrapolation) */

    /* local player */
    float    px, py, p_alt_f;
    uint32_t p_fire_age;      /* ms since last local shot (for muzzle flash) */
    uint8_t  p_alt, p_role, p_weapon, p_width, p_power, p_alive, p_invuln, p_special;
    uint8_t  p_is_mod;        /* local client is the moderator (may start New Game) */

    SnapProj   proj[MAX_PROJECTILES]; int n_proj;
    SnapExpl   expl[MAX_EXPL]; int n_expl;
    SnapSpark  spark[MAX_SPARK]; int n_spark;
    SnapTBul   tbul[MAX_TBUL]; int n_tbul;
    SnapRemote remotes[MAX_PLAYERS]; int n_remotes;
    SnapScore  board[MAX_PLAYERS]; int n_board;

    /* ids to suppress when the render thread rebuilds the deterministic world */
    uint32_t dead[MAX_DEAD_IDS]; int n_dead;   /* sorted ascending */

    /* HUD */
    int32_t  score;
    int32_t  hi_score;
    int      lives;
    uint8_t  gameover;
} RenderSnapshot;

void snap_init(void);
void snap_destroy(void);
void snap_publish(const RenderSnapshot *s);   /* sim -> */
int  snap_consume(RenderSnapshot *out);       /* render <- ; 1 if a snapshot exists */

#endif /* ECHELON_SNAPSHOT_H */
