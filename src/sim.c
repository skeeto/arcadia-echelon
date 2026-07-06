#include <string.h>
#include <math.h>
#include "sim.h"
#include "world.h"
#include "roles.h"

#define PROJ_R 7.0f

/* ---------------- dead-id set (sorted, deduped, tick-window pruned) --------- */

static int dead_has(GameState *g, uint32_t id)
{
    int lo = 0, hi = g->n_dead - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (g->dead[m] == id) return 1;
        if (g->dead[m] < id) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

static void dead_add(GameState *g, uint32_t id)
{
    int lo = 0, hi = g->n_dead - 1, pos;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (g->dead[m] == id) return;
        if (g->dead[m] < id) lo = m + 1; else hi = m - 1;
    }
    pos = lo;
    if (g->n_dead >= MAX_DEAD_IDS) {            /* full: drop smallest id */
        memmove(&g->dead[0], &g->dead[1], (MAX_DEAD_IDS - 1) * sizeof(uint32_t));
        g->n_dead--;
        if (pos > 0) pos--;
    }
    memmove(&g->dead[pos + 1], &g->dead[pos], (g->n_dead - pos) * sizeof(uint32_t));
    g->dead[pos] = id;
    g->n_dead++;
}

static int hit_add(GameState *g, uint32_t id, int dmg)
{
    int i;
    for (i = 0; i < g->n_hit; i++)
        if (g->hit[i].id == id) { g->hit[i].dmg += dmg; return g->hit[i].dmg; }
    if (g->n_hit < 96) { g->hit[g->n_hit].id = id; g->hit[g->n_hit].dmg = dmg; g->n_hit++; return dmg; }
    g->hit[0].id = id; g->hit[0].dmg = dmg; return dmg;   /* full: recycle slot 0 */
}

static void prune_ids(GameState *g)
{
    int32_t Tc   = (int32_t)(g->game_time_ms / SPAWN_QUANTUM_MS);
    int32_t span = (int32_t)((float)FIELD_H / SPAWN_QUANTUM_WORLD) + 1;
    int32_t lo   = Tc - GEN_MARGIN_TICKS - 6;
    int32_t hi   = Tc + span + GEN_MARGIN_TICKS + 6;
    int i, n = 0;
    for (i = 0; i < g->n_dead; i++) {
        int32_t t = (int32_t)((g->dead[i] >> 6) & 0x7FFFFF);
        if (t >= lo && t <= hi) g->dead[n++] = g->dead[i];
    }
    g->n_dead = n;
    n = 0;
    for (i = 0; i < g->n_hit; i++) {
        int32_t t = (int32_t)((g->hit[i].id >> 6) & 0x7FFFFF);
        if (t >= lo && t <= hi) g->hit[n++] = g->hit[i];
    }
    g->n_hit = n;
}

/* ---------------- geometry ------------------------------------------------- */

static int circ(float ax, float ay, float ar, float bx, float by, float br)
{
    float dx = ax - bx, dy = ay - by, rr = ar + br;
    return dx * dx + dy * dy <= rr * rr;
}

static int circ_rect(float cx, float cy, float cr, float rx, float ry, float hw, float hh)
{
    float nx = cx < rx - hw ? rx - hw : (cx > rx + hw ? rx + hw : cx);
    float ny = cy < ry - hh ? ry - hh : (cy > ry + hh ? ry + hh : cy);
    float dx = cx - nx, dy = cy - ny;
    return dx * dx + dy * dy <= cr * cr;
}

/* ---------------- events --------------------------------------------------- */

/* queue an event/fire for the host thread to broadcast this tick */
static void push_event(GameState *g, uint8_t kind, uint32_t id, int32_t points)
{
    if (g->n_oevents >= 32) return;
    g->oevents[g->n_oevents].kind = kind;
    g->oevents[g->n_oevents].id = id;
    g->oevents[g->n_oevents].points = points;
    g->n_oevents++;
}
static void push_fire(GameState *g, float x, float y, int alt)
{
    if (g->n_ofires >= 8) return;
    g->ofires[g->n_ofires].x = (int16_t)x;
    g->ofires[g->n_ofires].y = (int16_t)y;
    g->ofires[g->n_ofires].alt = (uint8_t)alt;
    g->n_ofires++;
}

/* one cue per id per tick, so an area kill doesn't stack a wall of explosions */
static void push_sound(GameState *g, uint8_t id)
{
    int i;
    for (i = 0; i < g->n_osounds; i++)
        if (g->osounds[i] == id) return;
    if (g->n_osounds < 16) g->osounds[g->n_osounds++] = id;
}

static void add_spark(GameState *g, float x, float y, uint8_t alt, uint8_t etype)
{
    if (g->n_spark >= MAX_SPARK) {
        memmove(&g->spark[0], &g->spark[1], (MAX_SPARK - 1) * sizeof(Spark));
        g->n_spark--;
    }
    g->spark[g->n_spark].x = x;
    g->spark[g->n_spark].y = y;
    g->spark[g->n_spark].alt = alt;
    g->spark[g->n_spark].etype = etype;
    g->spark[g->n_spark].start = g->last_ms;   /* == now_ms for this tick */
    g->spark[g->n_spark].seed = g->last_ms * 2654435761u +
                                (uint32_t)x * 40503u + (uint32_t)y * 12289u + 1u;
    g->n_spark++;
}

/* Local commits score AND broadcasts; remote events (sim_apply_event) only mark
 * the entity dead and never award the receiver score. */
static void kill_enemy(GameState *g, const Enemy *e)
{
    dead_add(g, e->id);
    g->p.score += e->points;
    add_spark(g, e->x, e->y, e->altitude, e->etype);
    push_sound(g, SND_EXPLODE);
    push_event(g, EV_KILL, e->id, e->points);
}

static void clear_obstacle(GameState *g, const Obstacle *o)
{
    dead_add(g, o->id);
    g->p.score += 50;
    push_event(g, EV_CLEARED, o->id, 0);
}

static void take_powerup(GameState *g, const Powerup *pu)
{
    dead_add(g, pu->id);
    switch (pu->kind) {
    case PU_WIDTH:   if (g->p.width_lvl < MAX_WIDTH_LVL) g->p.width_lvl++; break;
    case PU_POWER:   if (g->p.power_lvl < MAX_POWER_LVL) g->p.power_lvl++; break;
    case PU_1UP:     g->p.lives++; break;
    case PU_SPECIAL: if (g->p.special_charges < SPECIAL_MAX_CHARGES) g->p.special_charges++; break;
    case PU_WEAPON:  g->p.weapon4 = 1; break;
    }
    push_sound(g, pu->kind == PU_1UP ? SND_ONEUP : SND_POWERUP);
    push_event(g, EV_TAKEN, pu->id, 0);
}

static void add_expl(GameState *g, float x, float y, uint32_t now)
{
    if (g->n_expl >= MAX_EXPL) {
        memmove(&g->expl[0], &g->expl[1], (MAX_EXPL - 1) * sizeof(Expl));
        g->n_expl--;
    }
    g->expl[g->n_expl].x = x;
    g->expl[g->n_expl].y = y;
    g->expl[g->n_expl].start = now;
    g->n_expl++;
}

/* A bomb reaching the ground: area-of-effect damage to ground enemies and
 * obstacles within BOMB_RADIUS, plus a burst effect for the renderer. */
static void bomb_detonate(GameState *g, float bx, float by, int dmg, float radius, uint32_t now)
{
    int k;
    for (k = 0; k < g->wv.n_enemies; k++) {
        Enemy *e = &g->wv.enemies[k];
        if (e->kind != EK_GROUND || dead_has(g, e->id)) continue;
        if (circ(bx, by, radius, e->x, e->y, e->r))
            if (hit_add(g, e->id, dmg) >= e->hpmax) kill_enemy(g, e);
    }
    for (k = 0; k < g->wv.n_obstacles; k++) {
        Obstacle *o = &g->wv.obstacles[k];
        if (dead_has(g, o->id)) continue;
        if (circ_rect(bx, by, radius, o->x, o->y, o->w * 0.5f, o->h * 0.5f))
            if (hit_add(g, o->id, dmg) >= o->hpmax) clear_obstacle(g, o);
    }
    add_expl(g, bx, by, now);
    push_sound(g, SND_BOMB);
}

static void player_die(GameState *g, uint32_t now)
{
    g->p.alive = 0;
    push_sound(g, SND_DEATH);
    g->p.lives--;
    if (g->p.lives <= 0) { g->p.lives = 0; g->gameover = 1; }
    else                 { g->p.respawn_at = now + 1400; }
}

static void player_spawn(GameState *g, uint32_t now)
{
    g->p.x = FIELD_W * 0.5f;
    g->p.y = PLAYER_START_Y;
    g->p.altitude = ALT_LOW;
    g->p.alt_vis = 0.0f;
    g->p.eff_alt = ALT_LOW;
    g->p.alive = 1;
    g->p.invuln_until = now + 1500;
}

/* ---------------- lifecycle ------------------------------------------------ */

void sim_init(GameState *g, uint32_t seed, uint32_t now_ms)
{
    memset(g, 0, sizeof *g);
    g->seed = seed;
    clock_init(&g->clock, seed, now_ms);
    g->local_id = -1;
    g->last_ms = now_ms;
    g->p.role = ROLE_MULTI;      /* best solo default */
    g->p.lives = START_LIVES;
    player_spawn(g, now_ms);
    g->p.invuln_until = now_ms + 1500;
}

/* ---------------- collision passes ----------------------------------------- */

/* Air bolts only — bombs deal their damage via bomb_detonate() on the ground. */
static void collide_projectiles(GameState *g)
{
    int i, k;
    for (i = 0; i < g->n_proj; i++) {
        Proj *p = &g->proj[i];
        if (!p->active || p->is_bomb || p->cosmetic) continue;
        for (k = 0; k < g->wv.n_enemies && p->active; k++) {
            Enemy *e = &g->wv.enemies[k];
            if (e->kind != EK_AIR || e->altitude != p->altitude || dead_has(g, e->id)) continue;
            if (circ(p->x, p->y, PROJ_R, e->x, e->y, e->r)) {
                if (hit_add(g, e->id, p->dmg) >= e->hpmax) kill_enemy(g, e);
                if (!p->pierce) p->active = 0;
            }
        }
    }
}

/* Rebuild the set of live turret bullets deterministically from the current
 * on-screen turrets (skipping dead ones). Pure function of (live turrets,
 * gameTime) — no persistent bullet state, so all clients agree. */
static void gen_turret_bullets(GameState *g)
{
    uint32_t gt = g->game_time_ms;
    int i;
    g->n_tbul = 0;
    for (i = 0; i < g->wv.n_enemies; i++) {
        Enemy *e = &g->wv.enemies[i];
        uint32_t phase;
        long k;
        if (e->kind != EK_GROUND || dead_has(g, e->id)) continue;
        if (e->y < -20.0f || e->y > FIELD_H) continue;     /* only fire on screen */
        phase = ((uint32_t)e->id * 2654435761u) % TURRET_PERIOD_MS;
        for (k = ((long)gt - (long)phase) / TURRET_PERIOD_MS; k >= 0; k--) {
            uint32_t tf = phase + (uint32_t)k * TURRET_PERIOD_MS;
            uint32_t age;
            float by;
            if (tf > gt) continue;
            age = gt - tf;
            if (age > (uint32_t)TURRET_BULLET_LIFE) break;
            by = e->y + TURRET_BULLET_SPEED * (float)age / 1000.0f;
            if (by > FIELD_H + 20.0f) continue;
            if (g->n_tbul >= MAX_TBUL) return;
            g->tbul[g->n_tbul].x = e->x;
            g->tbul[g->n_tbul].y = by;
            g->n_tbul++;
        }
    }
}

static void collide_player(GameState *g, uint32_t now)
{
    int k;
    if (!g->p.alive || now < g->p.invuln_until) return;

    for (k = 0; k < g->wv.n_enemies; k++) {
        Enemy *e = &g->wv.enemies[k];
        if (dead_has(g, e->id)) continue;
        if (e->kind == EK_AIR && e->altitude == g->p.eff_alt &&
            circ(g->p.x, g->p.y, PLAYER_R, e->x, e->y, e->r)) { player_die(g, now); return; }
        if (e->kind == EK_GROUND && g->p.eff_alt == ALT_LOW &&
            circ(g->p.x, g->p.y, PLAYER_R, e->x, e->y, e->r)) { player_die(g, now); return; }
    }
    for (k = 0; k < g->wv.n_obstacles; k++) {
        Obstacle *o = &g->wv.obstacles[k];
        if (dead_has(g, o->id)) continue;
        if (g->p.eff_alt <= o->top_alt &&
            circ_rect(g->p.x, g->p.y, PLAYER_R, o->x, o->y, o->w * 0.5f, o->h * 0.5f)) {
            player_die(g, now); return;
        }
    }
    /* turret bullets travel along the ground — dodge by ascending or sidestep */
    if (g->p.eff_alt == ALT_LOW) {
        for (k = 0; k < g->n_tbul; k++)
            if (circ(g->p.x, g->p.y, PLAYER_R, g->tbul[k].x, g->tbul[k].y, TBULLET_R)) {
                player_die(g, now); return;
            }
    }
}

static void collect_powerups(GameState *g)
{
    int k;
    if (!g->p.alive) return;
    for (k = 0; k < g->wv.n_powerups; k++) {
        Powerup *pu = &g->wv.powerups[k];
        if (dead_has(g, pu->id)) continue;
        if (circ(g->p.x, g->p.y, PLAYER_R, pu->x, pu->y, pu->r + 4.0f))
            take_powerup(g, pu);
    }
}

/* Steer an F4 homing bolt toward the nearest live air enemy in its band. */
static void steer_homing(GameState *g, Proj *p, float dt)
{
    const Enemy *te = NULL;
    float best = 1e18f;
    int k;
    for (k = 0; k < g->wv.n_enemies; k++) {
        const Enemy *e = &g->wv.enemies[k];
        float dx, dy, d2;
        if (e->kind != EK_AIR || e->altitude != p->altitude || dead_has(g, e->id)) continue;
        dx = e->x - p->x; dy = e->y - p->y; d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; te = e; }
    }
    if (te) {
        float dx = te->x - p->x, dy = te->y - p->y, dl = sqrtf(dx * dx + dy * dy);
        if (dl > 1.0f) {
            float tvx = dx / dl * PROJ_SPEED, tvy = dy / dl * PROJ_SPEED;
            float kf = HOMING_TURN * dt, sp;
            if (kf > 1.0f) kf = 1.0f;
            p->vx += (tvx - p->vx) * kf;
            p->vy += (tvy - p->vy) * kf;
            sp = sqrtf(p->vx * p->vx + p->vy * p->vy);
            if (sp > 1.0f) { p->vx = p->vx / sp * PROJ_SPEED; p->vy = p->vy / sp * PROJ_SPEED; }
        }
    }
}

/* Start a fresh run: reset the shared timeline (so difficulty drops back to 0),
 * pick a new seed, and reset the local player. Propagates to peers via the
 * clock generation bump in their next heartbeat. */
static void sim_new_game(GameState *g, uint32_t now)
{
    uint32_t ns = now * 2654435761u + g->game_time_ms * 40503u + 0x1234567u;
    clock_reset(&g->clock, ns, now);
    g->seed = ns;
    g->n_proj = g->n_dead = g->n_hit = g->n_expl = g->n_spark = g->n_tbul = 0;
    g->n_oevents = g->n_ofires = g->n_osounds = 0;
    g->p.score = 0;
    g->p.lives = START_LIVES;
    g->p.width_lvl = g->p.power_lvl = 0;
    g->p.special_charges = 0;
    g->p.weapon4 = 0;
    g->p.weapon = 0;
    g->gameover = 0;
    player_spawn(g, now);
    push_sound(g, SND_ONEUP);
}

/* ---------------- step ----------------------------------------------------- */

void sim_advance(GameState *g, const Input *in, uint32_t now_ms)
{
    float dt = (float)(now_ms - g->last_ms) / 1000.0f;
    int i;

    g->last_ms = now_ms;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.06f) dt = 0.06f;

    g->n_oevents = 0;      /* outgoing events/fires/sounds collected this tick */
    g->n_ofires = 0;
    g->n_osounds = 0;

    if (in->new_game && now_ms - g->p.last_newgame > 500) {
        g->p.last_newgame = now_ms;
        sim_new_game(g, now_ms);
    }

    g->seed = g->clock.seed;
    g->game_time_ms = clock_time(&g->clock, now_ms);
    world_build(g->seed, g->game_time_ms, &g->wv);
    prune_ids(g);
    gen_turret_bullets(g);
    /* drop remotes we haven't heard from recently (they left/timed out) */
    {
        int r, n = 0;
        for (r = 0; r < g->n_remotes; r++)
            if (now_ms - g->remotes[r].last_seen < REMOTE_TIMEOUT_MS)
                g->remotes[n++] = g->remotes[r];
        g->n_remotes = n;
    }
    /* ease each remote's display position toward a capped prediction (kills the
     * extrapolation snap-back: the correction is blended in over ~TAU). */
    {
        int r;
        float a = dt / REMOTE_SMOOTH_TAU;
        if (a > 1.0f) a = 1.0f;
        for (r = 0; r < g->n_remotes; r++) {
            Remote  *rm  = &g->remotes[r];
            uint32_t age = now_ms - rm->last_seen;
            float ah = (age > REMOTE_PREDICT_MS) ? (float)REMOTE_PREDICT_MS : (float)age;
            float tx = rm->x + rm->vx * ah / 1000.0f;
            float ty = rm->y + rm->vy * ah / 1000.0f;
            if (tx < 0.0f) tx = 0.0f; else if (tx > FIELD_W) tx = FIELD_W;
            if (ty < 0.0f) ty = 0.0f; else if (ty > FIELD_H) ty = FIELD_H;
            rm->dx += (tx - rm->dx) * a;
            rm->dy += (ty - rm->dy) * a;
            {   /* ease remote altitude too */
                float da = (float)rm->alt - rm->alt_vis, st = ALT_SPEED * dt;
                if (da > st) rm->alt_vis += st;
                else if (da < -st) rm->alt_vis -= st;
                else rm->alt_vis = (float)rm->alt;
            }
        }
    }

    if (g->gameover) {
        if (in->restart) {
            g->gameover = 0;
            g->p.score = 0;
            g->p.lives = START_LIVES;
            g->p.width_lvl = g->p.power_lvl = 0;
            g->p.weapon = 0;
            player_spawn(g, now_ms);
        }
    } else if (g->p.alive) {
        /* role / weapon / altitude */
        if (in->role_sel >= 1 && in->role_sel <= ROLE_COUNT) {
            int nr = in->role_sel - 1;
            if (nr != g->p.role) { g->p.role = (uint8_t)nr; g->p.weapon = 0; }
        }
        if (in->weapon >= 1) {
            int idx = in->weapon - 1;
            if (idx < role_num_weapons(g->p.role)) g->p.weapon = (uint8_t)idx;
            else if (in->weapon == 4 && g->p.weapon4) g->p.weapon = 3;   /* F4 unlock */
        }
        if (in->ascend && now_ms - g->p.last_alt > ALT_CHANGE_MS && g->p.altitude < ALT_HIGH) {
            g->p.altitude++; g->p.last_alt = now_ms;
        }
        if (in->descend && now_ms - g->p.last_alt > ALT_CHANGE_MS && g->p.altitude > ALT_LOW) {
            g->p.altitude--; g->p.last_alt = now_ms;
        }
        /* ease the visual/effective altitude toward the target band */
        {
            float target = (float)g->p.altitude, step = ALT_SPEED * dt, d = target - g->p.alt_vis;
            if (d > step) g->p.alt_vis += step;
            else if (d < -step) g->p.alt_vis -= step;
            else g->p.alt_vis = target;
            g->p.eff_alt = (uint8_t)(g->p.alt_vis + 0.5f);
            if (g->p.eff_alt >= ALT_COUNT) g->p.eff_alt = ALT_COUNT - 1;
        }
        /* movement */
        g->p.x += (float)(in->right - in->left) * PLAYER_SPEED * dt;
        g->p.y += (float)(in->down - in->up) * PLAYER_SPEED * dt;
        if (g->p.x < PLAYER_R)            g->p.x = PLAYER_R;
        if (g->p.x > FIELD_W - PLAYER_R)  g->p.x = FIELD_W - PLAYER_R;
        if (g->p.y < FIELD_H * 0.35f)     g->p.y = FIELD_H * 0.35f;
        if (g->p.y > FIELD_H - 30.0f)     g->p.y = FIELD_H - 30.0f;
        /* fire */
        if (in->fire && now_ms - g->p.last_fire >= (uint32_t)FIRE_COOLDOWN_MS) {
            Proj tmp[32]; int nc = 0;
            int cd = role_fire(g->p.role, g->p.weapon, g->p.width_lvl, g->p.power_lvl,
                               g->p.x, g->p.y - PLAYER_R, g->p.eff_alt,
                               g->local_id, tmp, 32, &nc);
            if (cd > 0) {
                for (i = 0; i < nc; i++)
                    if (g->n_proj < MAX_PROJECTILES) g->proj[g->n_proj++] = tmp[i];
                g->p.last_fire = now_ms;
                push_fire(g, g->p.x, g->p.y - PLAYER_R, g->p.eff_alt);
                push_sound(g, SND_SHOOT);
            }
        }
        /* role special (F5) */
        if (in->special && g->p.special_charges > 0 &&
            now_ms - g->p.last_special >= (uint32_t)SPECIAL_COOLDOWN_MS) {
            Proj tmp[64]; int nc = 0, j;
            role_special(g->p.role, g->p.width_lvl, g->p.power_lvl,
                         g->p.x, g->p.y - PLAYER_R, g->p.eff_alt, g->local_id, tmp, 64, &nc);
            for (j = 0; j < nc; j++)
                if (g->n_proj < MAX_PROJECTILES) g->proj[g->n_proj++] = tmp[j];
            g->p.special_charges--;
            g->p.last_special = now_ms;
            push_fire(g, g->p.x, g->p.y - PLAYER_R, g->p.eff_alt);
            push_sound(g, SND_SHOOT);
        }
    } else {   /* dead, waiting to respawn */
        if (now_ms >= g->p.respawn_at) player_spawn(g, now_ms);
    }

    /* projectiles advance regardless */
    for (i = 0; i < g->n_proj; i++) {
        Proj *p = &g->proj[i];
        if (!p->active) continue;
        if (p->homing && !p->is_bomb) steer_homing(g, p, dt);
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        if (p->is_bomb) {
            p->fuse -= dt * 1000.0f;
            p->fall = p->fuse / (float)BOMB_FALL_MS;
            if (p->fall < 0.0f) p->fall = 0.0f;
            if (p->fuse <= 0.0f) {
                if (p->cosmetic) add_expl(g, p->x, p->y, now_ms);          /* show only */
                else bomb_detonate(g, p->x, p->y, p->dmg, p->radius, now_ms);
                p->active = 0;
            }
        } else if (p->y < -20.0f || p->y > FIELD_H + 20.0f ||
                   p->x < -20.0f || p->x > FIELD_W + 20.0f) {
            p->active = 0;
        }
    }
    /* age out finished bomb bursts and sparks */
    {
        int n = 0;
        for (i = 0; i < g->n_expl; i++)
            if (now_ms - g->expl[i].start < EXPLOSION_MS) g->expl[n++] = g->expl[i];
        g->n_expl = n;
        n = 0;
        for (i = 0; i < g->n_spark; i++)
            if (now_ms - g->spark[i].start < SPARK_MS) g->spark[n++] = g->spark[i];
        g->n_spark = n;
    }
    /* compact the projectile array */
    {
        int n = 0;
        for (i = 0; i < g->n_proj; i++)
            if (g->proj[i].active) g->proj[n++] = g->proj[i];
        g->n_proj = n;
    }

    collide_projectiles(g);
    collide_player(g, now_ms);
    collect_powerups(g);

    if (g->p.score > g->hi_score) g->hi_score = g->p.score;
}

/* ---------------- remote players ------------------------------------------- */

void sim_apply_state(GameState *g, const MsgState *m, uint32_t now_ms)
{
    int i, slot = -1;
    if (m->sid == g->local_id) return;      /* ignore our own broadcast echo */
    for (i = 0; i < g->n_remotes; i++)
        if (g->remotes[i].id == m->sid) { slot = i; break; }
    if (slot < 0) {
        if (g->n_remotes >= MAX_PLAYERS) return;
        slot = g->n_remotes++;
        memset(&g->remotes[slot], 0, sizeof(Remote));
        g->remotes[slot].id = m->sid;
        g->remotes[slot].x = g->remotes[slot].dx = (float)m->x;
        g->remotes[slot].y = g->remotes[slot].dy = (float)m->y;
        g->remotes[slot].alt_vis = (float)m->alt;
        g->remotes[slot].last_seen = now_ms;   /* no velocity from a single sample */
    }
    {
        Remote  *r  = &g->remotes[slot];
        uint32_t dt = now_ms - r->last_seen;
        float    nx = (float)m->x, ny = (float)m->y;
        if (dt >= 16 && dt <= 500) {            /* derive velocity for prediction */
            float s = 1000.0f / (float)dt;
            r->vx = (nx - r->x) * s;
            r->vy = (ny - r->y) * s;
        } else {
            r->vx = r->vy = 0.0f;
        }
        r->x = nx; r->y = ny;
        r->alt = m->alt; r->role = m->role; r->weapon = m->weapon;
        r->width = m->width; r->power = m->power;
        r->alive = (uint8_t)(m->flags & 1);
        r->score = m->score;
        if (m->best > r->best) r->best = m->best;
        r->last_seen = now_ms;
    }
}

/* A peer's client-authoritative event: remove the entity locally (no score to
 * us). All three kinds resolve to "mark this id gone"; dead_add dedups. */
void sim_apply_event(GameState *g, const MsgEvent *m)
{
    if (m->sid == g->local_id) return;
    dead_add(g, m->id);
}

/* A peer fired: reconstruct its shot pattern from the role/weapon we last heard
 * for it, and add the projectiles as cosmetic (no collision on our side). */
void sim_apply_fire(GameState *g, const MsgFire *m, uint32_t now_ms)
{
    int i, slot = -1, nc = 0, j;
    Proj tmp[64];
    (void)now_ms;
    if (m->sid == g->local_id) return;
    for (i = 0; i < g->n_remotes; i++)
        if (g->remotes[i].id == m->sid) { slot = i; break; }
    if (slot < 0) return;   /* unknown peer — skip the cosmetic shot */
    role_fire(g->remotes[slot].role, g->remotes[slot].weapon,
              g->remotes[slot].width, g->remotes[slot].power,
              (float)m->x, (float)m->y, m->alt, m->sid, tmp, 64, &nc);
    for (j = 0; j < nc; j++) {
        tmp[j].cosmetic = 1;
        if (g->n_proj < MAX_PROJECTILES) g->proj[g->n_proj++] = tmp[j];
    }
}

void sim_set_name(GameState *g, int id, const char *name)
{
    int i;
    if (!name) return;
    if (id == g->local_id) {
        strncpy(g->local_name, name, 27); g->local_name[27] = 0;
        return;
    }
    for (i = 0; i < g->n_remotes; i++)
        if (g->remotes[i].id == id) {
            strncpy(g->remotes[i].name, name, 27); g->remotes[i].name[27] = 0;
            return;
        }
}

/* ---------------- snapshot ------------------------------------------------- */

void sim_snapshot(GameState *g, RenderSnapshot *s, uint32_t now_ms)
{
    int i, n = 0;
    memset(s, 0, sizeof *s);
    s->seed = g->seed;
    s->game_time_ms = g->game_time_ms;
    s->stamp_ms = now_ms;

    s->px = g->p.x;   s->py = g->p.y;
    s->p_alt_f = g->p.alt_vis;
    s->p_alt = g->p.eff_alt; s->p_role = g->p.role; s->p_weapon = g->p.weapon;
    s->p_width = g->p.width_lvl; s->p_power = g->p.power_lvl;
    s->p_alive = g->p.alive;
    s->p_invuln = (now_ms < g->p.invuln_until) ? 1 : 0;
    s->p_special = g->p.special_charges;
    s->p_fire_age = now_ms - g->p.last_fire;

    for (i = 0; i < g->n_proj && n < MAX_PROJECTILES; i++) {
        if (!g->proj[i].active) continue;
        s->proj[n].x = g->proj[i].x;
        s->proj[n].y = g->proj[i].y;
        s->proj[n].vy = g->proj[i].vy;
        s->proj[n].fall = g->proj[i].fall;
        s->proj[n].altitude = g->proj[i].altitude;
        s->proj[n].is_bomb = g->proj[i].is_bomb;
        s->proj[n].active = 1;
        n++;
    }
    s->n_proj = n;

    {
        int e, m = 0;
        for (e = 0; e < g->n_expl && m < MAX_EXPL; e++) {
            uint32_t age = now_ms - g->expl[e].start;
            if (age >= EXPLOSION_MS) continue;
            s->expl[m].x = g->expl[e].x;
            s->expl[m].y = g->expl[e].y;
            s->expl[m].progress = (float)age / (float)EXPLOSION_MS;
            m++;
        }
        s->n_expl = m;
    }
    {
        int e, m = 0;
        for (e = 0; e < g->n_spark && m < MAX_SPARK; e++) {
            uint32_t age = now_ms - g->spark[e].start;
            if (age >= SPARK_MS) continue;
            s->spark[m].x = g->spark[e].x;
            s->spark[m].y = g->spark[e].y;
            s->spark[m].alt = g->spark[e].alt;
            s->spark[m].etype = g->spark[e].etype;
            s->spark[m].seed = g->spark[e].seed;
            s->spark[m].progress = (float)age / (float)SPARK_MS;
            m++;
        }
        s->n_spark = m;
    }
    {
        int i, m = 0;
        for (i = 0; i < g->n_tbul && m < MAX_TBUL; i++) {
            s->tbul[m].x = g->tbul[i].x;
            s->tbul[m].y = g->tbul[i].y;
            m++;
        }
        s->n_tbul = m;
    }

    s->n_dead = g->n_dead < MAX_DEAD_IDS ? g->n_dead : MAX_DEAD_IDS;
    memcpy(s->dead, g->dead, s->n_dead * sizeof(uint32_t));

    {
        int r, k = 0;
        for (r = 0; r < g->n_remotes && k < MAX_PLAYERS; r++) {
            s->remotes[k].x = g->remotes[r].dx;
            s->remotes[k].y = g->remotes[r].dy;
            s->remotes[k].alt_f = g->remotes[r].alt_vis;
            s->remotes[k].role = g->remotes[r].role;
            s->remotes[k].alive = g->remotes[r].alive;
            k++;
        }
        s->n_remotes = k;
    }

    /* high-score board: local + remotes, by best desc */
    {
        int i, j, k = 0;
        const char *nm = g->local_name[0] ? g->local_name : "YOU";
        strncpy(s->board[k].name, nm, 15); s->board[k].name[15] = 0;
        s->board[k].best = g->hi_score; s->board[k].is_local = 1; k++;
        for (i = 0; i < g->n_remotes && k < MAX_PLAYERS; i++) {
            nm = g->remotes[i].name[0] ? g->remotes[i].name : "PLAYER";
            strncpy(s->board[k].name, nm, 15); s->board[k].name[15] = 0;
            s->board[k].best = g->remotes[i].best; s->board[k].is_local = 0; k++;
        }
        for (i = 1; i < k; i++) {              /* insertion sort, best first */
            SnapScore t = s->board[i];
            j = i - 1;
            while (j >= 0 && s->board[j].best < t.best) { s->board[j+1] = s->board[j]; j--; }
            s->board[j+1] = t;
        }
        s->n_board = k;
    }

    s->score = g->p.score;
    s->hi_score = g->hi_score;
    s->lives = g->p.lives;
    s->gameover = g->gameover;
}
