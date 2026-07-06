/*
 * config.h — Echelon tunable constants and fixed capacities.
 *
 * Everything that shapes feel or bounds memory lives here so tuning is one file.
 * No code, just #defines and a couple of derived inline helpers-by-macro.
 */
#ifndef ECHELON_CONFIG_H
#define ECHELON_CONFIG_H

/* ---- Play field (world units; letterboxed & centered into the canvas) ---- */
#define FIELD_W        1000
#define FIELD_H         750

/* ---- Altitude bands ---- */
enum { ALT_LOW = 0, ALT_MID = 1, ALT_HIGH = 2, ALT_COUNT = 3 };
#define ALT_OFFSET      26.0f   /* screen-units a sprite lifts per altitude band */

/* ---- Scrolling & spawn quantization ----
 * scroll_y = SCROLL_SPEED * gameTime_ms / 1000  (continuous, for motion/draw)
 * T        = gameTime_ms / SPAWN_QUANTUM_MS     (integer, for spawns + ids)     */
#define SCROLL_SPEED      190.0f          /* world units per second              */
#define SPAWN_QUANTUM_MS  100             /* one spawn opportunity per 100 ms     */
#define SPAWN_QUANTUM_WORLD  (SCROLL_SPEED * SPAWN_QUANTUM_MS / 1000.0f) /* 19.0 */

/* How far off the top/bottom edge (in ticks) we still generate, so entities that
 * weave or that just left/entered are covered. */
#define GEN_MARGIN_TICKS  10

/* ---- Fixed capacities (no heap in the hot path) ---- */
#define MAX_PLAYERS      8
#define MAX_ENEMIES      160
#define MAX_OBSTACLES    64
#define MAX_POWERUPS     48
#define MAX_PROJECTILES  256
#define MAX_DEAD_IDS     256    /* recent killed/cleared/taken ids we suppress   */
#define MAX_HIGHSCORES   8

/* ---- Player ---- */
#define PLAYER_SPEED     360.0f  /* world units/sec horizontal & vertical         */
#define PLAYER_START_Y   (FIELD_H * 0.80f)
#define PLAYER_R         18.0f   /* collision radius                              */
#define START_LIVES      3
#define ALT_CHANGE_MS    170     /* debounce for Home/End altitude changes        */
#define ALT_SPEED        6.0f    /* altitude bands traversed per second (eased)   */

/* ---- Weapons / projectiles ---- */
#define PROJ_SPEED       620.0f  /* air bolt speed (world units/sec, upward)      */
#define BOMB_FALL_MS     440     /* time a bomb arcs from launch to ground burst  */
#define BOMB_FWD         240.0f  /* bomb forward speed while falling               */
#define BOMB_RADIUS      95.0f   /* area-of-effect burst radius (ground)          */
#define FIRE_COOLDOWN_MS 140     /* base gap between shots                        */
#define MAX_WIDTH_LVL    3
#define MAX_POWER_LVL    3
#define HOMING_TURN      7.0f    /* how hard F4 homing bolts steer toward a target */

/* ---- Explosions (bomb bursts, for render) ---- */
#define MAX_EXPL         32
#define EXPLOSION_MS     340

/* ---- Enemy death breakup (fragments, for render) ---- */
#define MAX_SPARK        48
#define SPARK_MS         420     /* debris lifetime                               */
#define MUZZLE_MS        90      /* how long a muzzle flash shows after a shot     */

/* ---- Ground turret fire (deterministic, dodgeable) ---- */
#define MAX_TBUL             160
#define TURRET_PERIOD_MS     1400   /* time between a turret's shots               */
#define TURRET_BULLET_SPEED  90.0f  /* extra downward screen speed over the scroll */
#define TURRET_BULLET_LIFE   6000   /* ms a bullet is generated for                */
#define TBULLET_R            8.0f

/* ---- Role specials (F5), charged by PU_SPECIAL ---- */
#define SPECIAL_MAX_CHARGES     3
#define SPECIAL_COOLDOWN_MS     600
#define FIGHTER_SPECIAL_BOLTS   13
#define FIGHTER_SPECIAL_SPREAD  150.0f
#define BOMBER_SPECIAL_RADIUS   220.0f
#define BOMBER_SPECIAL_DMG      6

/* ---- Fixed timestep ---- */
#define SIM_HZ           32
#define SIM_DT_MS        (1000 / SIM_HZ)   /* ~31 ms                              */

/* ---- Networking / clock sync ---- */
#define NET_CHANNEL         0     /* Arcadia channel we broadcast on              */
#define STATE_SEND_MS       100   /* PLAYER_STATE broadcast period (~10 Hz)       */
#define HEARTBEAT_MS        1000  /* HEARTBEAT (seed, gameTime) period            */
#define ROSTER_POLL_MS      1000  /* re-poll ar_get_player_list for anchor/id     */
#define REMOTE_TIMEOUT_MS   3500  /* drop a remote we haven't heard from          */
#define MAX_PLAUSIBLE_LEAD  3000  /* ignore non-anchor clocks more than this ahead */
#define CATCHUP_STEP        50    /* max ms of slew added per heartbeat (rate cap) */
#define REMOTE_PREDICT_MS   120   /* how far ahead we predict a remote ship (ms)   */
#define REMOTE_SMOOTH_TAU   0.09f /* display-position easing time constant (s)      */

#endif /* ECHELON_CONFIG_H */
