#include <windows.h>
#include <GL/gl.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render.h"
#include "config.h"
#include "types.h"
#include "world.h"
#include "roles.h"
#include "snapshot.h"
#include "glfont.h"

static const char GL_CLASS[] = "EchelonGLChildWindow";

static HWND          s_glwnd;
static HANDLE        s_thread;
static volatile LONG s_run;

/* ---- 3D field mapping -----------------------------------------------------
 * The sim is 2D field coords (x in [0,FIELD_W], y in [0,FIELD_H], y down toward
 * the player) + an altitude band. The renderer maps those onto a tilted 3D
 * runway: field-x -> world X, altitude -> world Y (height), field-y -> world Z
 * (depth; near the player at Z~0, receding to -F3_DEPTH in the distance). */
#define F3_XSCALE   0.52f
#define F3_DEPTH    900.0f
#define ALT_BASE    18.0f
#define ALT_H       40.0f

#define CAM_PITCH   20.0f
#define CAM_H       170.0f
#define CAM_DIST    360.0f
#define NEARP       50.0
#define FARP        2600.0

static float mapX(float fx)  { return (fx - FIELD_W * 0.5f) * F3_XSCALE; }
static float mapZ(float fy)  { return (fy - (float)FIELD_H) / (float)FIELD_H * F3_DEPTH; }
static float mapY(int alt)   { return ALT_BASE + (float)alt * ALT_H; }
static float mapYf(float a)  { return ALT_BASE + a * ALT_H; }
#define FHW (FIELD_W * 0.5f * F3_XSCALE)   /* field half-width in world X */

/* ---- colors --------------------------------------------------------------- */
static const float ALT_COLOR[ALT_COUNT][3] = {
    { 1.00f, 0.45f, 0.40f },   /* low  — warm red   */
    { 1.00f, 0.85f, 0.35f },   /* mid  — amber      */
    { 0.45f, 0.85f, 1.00f },   /* high — cyan       */
};
static const float ROLE_COLOR[3][3] = {
    { 0.55f, 0.90f, 1.00f }, { 1.00f, 0.70f, 0.30f }, { 0.60f, 1.00f, 0.65f },
};
static const float PU_COLOR[PU_COUNT][3] = {
    { 0.40f, 1.00f, 0.50f },   /* width   */
    { 1.00f, 0.40f, 0.40f },   /* power   */
    { 1.00f, 1.00f, 1.00f },   /* 1up     */
    { 1.00f, 0.40f, 1.00f },   /* special */
    { 0.40f, 0.70f, 1.00f },   /* weapon  */
};

/* ---- window plumbing ------------------------------------------------------ */
static HINSTANCE self_instance(void)
{
    HINSTANCE hi = 0;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(const void *)&self_instance, &hi);
    return hi;
}
static LRESULT CALLBACK gl_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_ERASEBKGND) return 1;
    return DefWindowProcA(h, m, w, l);
}

static int dead_has(const RenderSnapshot *s, uint32_t id)
{
    int lo = 0, hi = s->n_dead - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (s->dead[m] == id) return 1;
        if (s->dead[m] < id) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

/* ---- 3D primitives -------------------------------------------------------- */

/* A triangle that computes and emits its own (unnormalized) face normal. */
static void tri3(float ax, float ay, float az, float bx, float by, float bz,
                 float cx, float cy, float cz)
{
    float ux = bx - ax, uy = by - ay, uz = bz - az;
    float vx = cx - ax, vy = cy - ay, vz = cz - az;
    glNormal3f(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx);
    glVertex3f(ax, ay, az); glVertex3f(bx, by, bz); glVertex3f(cx, cy, cz);
}

/* Axis-aligned box centered on origin, half extents (hx,hy,hz). */
static void box(float hx, float hy, float hz)
{
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);  glVertex3f(-hx,-hy, hz); glVertex3f( hx,-hy, hz); glVertex3f( hx, hy, hz); glVertex3f(-hx, hy, hz);
    glNormal3f(0, 0,-1);  glVertex3f( hx,-hy,-hz); glVertex3f(-hx,-hy,-hz); glVertex3f(-hx, hy,-hz); glVertex3f( hx, hy,-hz);
    glNormal3f(-1,0, 0);  glVertex3f(-hx,-hy,-hz); glVertex3f(-hx,-hy, hz); glVertex3f(-hx, hy, hz); glVertex3f(-hx, hy,-hz);
    glNormal3f(1, 0, 0);  glVertex3f( hx,-hy, hz); glVertex3f( hx,-hy,-hz); glVertex3f( hx, hy,-hz); glVertex3f( hx, hy, hz);
    glNormal3f(0, 1, 0);  glVertex3f(-hx, hy, hz); glVertex3f( hx, hy, hz); glVertex3f( hx, hy,-hz); glVertex3f(-hx, hy,-hz);
    glNormal3f(0,-1, 0);  glVertex3f(-hx,-hy,-hz); glVertex3f( hx,-hy,-hz); glVertex3f( hx,-hy, hz); glVertex3f(-hx,-hy, hz);
    glEnd();
}

/* Square pyramid, base on y=0 (corners ±s), apex at (0,h,0). */
static void pyramid(float s, float h)
{
    glBegin(GL_TRIANGLES);
    tri3(-s,0,-s,  s,0,-s,  0,h,0);
    tri3( s,0,-s,  s,0, s,  0,h,0);
    tri3( s,0, s, -s,0, s,  0,h,0);
    tri3(-s,0, s, -s,0,-s,  0,h,0);
    glEnd();
}

/* Octahedron (a 3D diamond), radius r. */
static void octa(float r)
{
    glBegin(GL_TRIANGLES);
    tri3( r,0,0, 0,r,0, 0,0,r);  tri3( 0,r,0,-r,0,0, 0,0,r);
    tri3(-r,0,0, 0,-r,0,0,0,r);  tri3( 0,-r,0, r,0,0,0,0,r);
    tri3( 0,r,0, r,0,0, 0,0,-r); tri3(-r,0,0, 0,r,0, 0,0,-r);
    tri3( 0,-r,0,-r,0,0,0,0,-r); tri3( r,0,0, 0,-r,0,0,0,-r);
    glEnd();
}

/* Forward-pointing dart (ship), nose at -Z, scaled by s. */
static void dart(float s)
{
    float nz = -1.5f * s, bz = 0.8f * s, bw = 0.95f * s, th = 0.85f * s;
    glBegin(GL_TRIANGLES);
    tri3(0, 0.15f*s, nz,  -bw, 0, bz,  0, th, bz);   /* left top   */
    tri3(0, 0.15f*s, nz,   0, th, bz,  bw, 0, bz);   /* right top  */
    tri3(0, 0.15f*s, nz,   bw, 0, bz, -bw, 0, bz);   /* belly      */
    tri3(-bw, 0, bz,       bw, 0, bz,  0, th, bz);   /* tail       */
    glEnd();
}

/* Flat dark shadow blob on the ground plane at field (fx,fy). */
static void ground_shadow(float fx, float fy, float rad)
{
    float X = mapX(fx), Z = mapZ(fy), rx = rad * F3_XSCALE, rz = rad * 0.8f;
    glColor4f(0.0f, 0.0f, 0.0f, 0.30f);
    glBegin(GL_QUADS);
    glVertex3f(X - rx, 0.6f, Z - rz); glVertex3f(X + rx, 0.6f, Z - rz);
    glVertex3f(X + rx, 0.6f, Z + rz); glVertex3f(X - rx, 0.6f, Z + rz);
    glEnd();
}

/* ---- scene ---------------------------------------------------------------- */

static void set_projection(int w, int h)
{
    float sx = (float)w / FIELD_W, sy = (float)h / FIELD_H;
    float s  = sx < sy ? sx : sy;
    int   vw = (int)(FIELD_W * s), vh = (int)(FIELD_H * s);
    double aspect, top, right;
    glViewport((w - vw) / 2, (h - vh) / 2, vw, vh);
    aspect = (double)vw / (vh > 0 ? vh : 1);
    top    = NEARP * tan(60.0 * 3.14159265 / 180.0 / 2.0);
    right  = top * aspect;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-right, right, -top, top, NEARP, FARP);
    glMatrixMode(GL_MODELVIEW);
}

static void camera(void)
{
    glLoadIdentity();
    glRotatef(CAM_PITCH, 1, 0, 0);
    glTranslatef(0.0f, -CAM_H, -CAM_DIST);
}

/* cheap deterministic hash -> [0,1), for fixed star placement */
static float hashf(unsigned i)
{
    i = (i ^ 61u) ^ (i >> 16);
    i *= 9u; i ^= i >> 4; i *= 0x27d4eb2du; i ^= i >> 15;
    return (float)(i & 0xFFFFFF) / (float)0x1000000;
}

/* Parallax starfield in the sky, scrolling slower than the ground for depth. */
static void draw_stars(float scroll)
{
    const int   N = 150;
    const float DEPTH = F3_DEPTH * 1.6f;
    int i;
    glDisable(GL_LIGHTING);
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (i = 0; i < N; i++) {
        float sx = (hashf(i * 3 + 1) * 2.0f - 1.0f) * FHW * 2.4f;
        float sy = 55.0f + hashf(i * 3 + 2) * 300.0f;
        float z  = -fmodf(scroll * 0.22f + hashf(i * 3 + 3) * DEPTH, DEPTH);
        float b  = 0.35f + 0.6f * hashf(i * 5 + 7);
        glColor3f(b, b, b * 1.08f);
        glVertex3f(sx, sy, z);
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

static void draw_ground(float scroll)
{
    float sp = 64.0f, farZ = -F3_DEPTH;
    long k0 = (long)floorf(scroll / sp), k1 = (long)floorf((scroll + FIELD_H) / sp) + 1, k;
    /* ground quad */
    glColor3f(0.10f, 0.13f, 0.10f);
    glBegin(GL_QUADS);
    glVertex3f(-FHW, 0, 0); glVertex3f(FHW, 0, 0); glVertex3f(FHW, 0, farZ); glVertex3f(-FHW, 0, farZ);
    glEnd();
    /* grid lines */
    glColor3f(0.16f, 0.22f, 0.20f);
    glBegin(GL_LINES);
    for (k = k0; k <= k1; k++) {                              /* transverse */
        float fy = (float)FIELD_H - ((float)k * sp - scroll);
        float Z = mapZ(fy);
        glVertex3f(-FHW, 0.2f, Z); glVertex3f(FHW, 0.2f, Z);
    }
    {                                                        /* longitudinal */
        int i;
        for (i = -2; i <= 2; i++) {
            float X = FHW * (float)i / 2.0f;
            glVertex3f(X, 0.2f, 0.0f); glVertex3f(X, 0.2f, farZ);
        }
    }
    glEnd();
}

static void draw_shadows(const RenderSnapshot *s, const WorldView *wv)
{
    int i;
    glDepthMask(GL_FALSE);
    for (i = 0; i < wv->n_obstacles; i++)
        if (!dead_has(s, wv->obstacles[i].id))
            ground_shadow(wv->obstacles[i].x, wv->obstacles[i].y, wv->obstacles[i].w * 0.5f);
    for (i = 0; i < wv->n_enemies; i++)
        if (!dead_has(s, wv->enemies[i].id))
            ground_shadow(wv->enemies[i].x, wv->enemies[i].y, wv->enemies[i].r);
    for (i = 0; i < wv->n_powerups; i++)
        if (!dead_has(s, wv->powerups[i].id))
            ground_shadow(wv->powerups[i].x, wv->powerups[i].y, wv->powerups[i].r);
    if (s->p_alive)
        ground_shadow(s->px, s->py, 18.0f);
    for (i = 0; i < s->n_remotes; i++)
        if (s->remotes[i].alive)
            ground_shadow(s->remotes[i].x, s->remotes[i].y, 18.0f);
    glDepthMask(GL_TRUE);
}

static void put(float fx, float fy, float y) { glTranslatef(mapX(fx), y, mapZ(fy)); }

static void draw_volumes(const RenderSnapshot *s, const WorldView *wv, uint32_t rt)
{
    int i;
    /* obstacles: tall columns from the ground through the altitude bands */
    for (i = 0; i < wv->n_obstacles; i++) {
        const Obstacle *o = &wv->obstacles[i];
        float top = mapY(o->top_alt);
        if (dead_has(s, o->id)) continue;
        glColor3f(0.42f, 0.40f, 0.38f);
        glPushMatrix();
        put(o->x, o->y, top * 0.5f);
        box(o->w * 0.5f * F3_XSCALE, top * 0.5f, o->h * 0.5f * 0.8f);
        glPopMatrix();
    }
    /* ground enemies: pyramids on the deck */
    for (i = 0; i < wv->n_enemies; i++) {
        const Enemy *e = &wv->enemies[i];
        if (e->kind != EK_GROUND || dead_has(s, e->id)) continue;
        glColor3f(0.85f, 0.35f, 0.30f);
        glPushMatrix();
        put(e->x, e->y, 0.0f);
        pyramid(e->r * F3_XSCALE * 1.4f, e->r * 1.3f);
        glPopMatrix();
    }
    /* powerups: spinning octahedra */
    for (i = 0; i < wv->n_powerups; i++) {
        const Powerup *pu = &wv->powerups[i];
        const float *c;
        if (dead_has(s, pu->id)) continue;
        c = PU_COLOR[pu->kind < PU_COUNT ? pu->kind : 0];
        glColor3f(c[0], c[1], c[2]);
        glPushMatrix();
        put(pu->x, pu->y, mapY(ALT_MID));
        glRotatef((float)(rt % 3600) * 0.12f, 0, 1, 0);
        octa(pu->r);
        glPopMatrix();
    }
    /* air enemies: cubes at their altitude */
    for (i = 0; i < wv->n_enemies; i++) {
        const Enemy *e = &wv->enemies[i];
        const float *c;
        if (e->kind != EK_AIR || dead_has(s, e->id)) continue;
        c = ALT_COLOR[e->altitude < ALT_COUNT ? e->altitude : 0];
        glPushMatrix();
        put(e->x, e->y, mapY(e->altitude));
        if (e->etype == 1) {                     /* heavy: brighter octahedron */
            glColor3f(0.5f + c[0] * 0.5f, 0.5f + c[1] * 0.5f, 0.5f + c[2] * 0.5f);
            octa(e->r);
        } else {                                 /* grunt: cube */
            glColor3f(c[0], c[1], c[2]);
            box(e->r * 0.85f, e->r * 0.85f, e->r * 0.85f);
        }
        glPopMatrix();
    }
    /* remote players (dimmed to distinguish from the local ship) */
    for (i = 0; i < s->n_remotes; i++) {
        const SnapRemote *rp = &s->remotes[i];
        const float *c;
        if (!rp->alive) continue;
        c = ROLE_COLOR[rp->role < 3 ? rp->role : 2];
        glColor3f(c[0] * 0.82f, c[1] * 0.82f, c[2] * 0.82f);
        glPushMatrix();
        put(rp->x, rp->y, mapYf(rp->alt_f));
        dart(20.0f);
        glPopMatrix();
    }
    /* player dart */
    if (s->p_alive && !(s->p_invuln && ((rt / 90) & 1))) {
        const float *c = ROLE_COLOR[s->p_role < 3 ? s->p_role : 2];
        glColor3f(c[0], c[1], c[2]);
        glPushMatrix();
        put(s->px, s->py, mapYf(s->p_alt_f));
        dart(20.0f);
        glPopMatrix();
    }
    /* muzzle flash just after firing */
    if (s->p_alive && s->p_fire_age < MUZZLE_MS) {
        float f = 1.0f - (float)s->p_fire_age / (float)MUZZLE_MS;
        glDisable(GL_LIGHTING);
        glColor4f(1.0f, 0.95f, 0.6f, f);
        glPushMatrix();
        put(s->px, s->py - 20.0f, mapYf(s->p_alt_f));
        octa(5.0f + 7.0f * f);
        glPopMatrix();
        glEnable(GL_LIGHTING);
    }
}

static void draw_projectiles(const RenderSnapshot *s, float dsec)
{
    int i;
    glDisable(GL_LIGHTING);                 /* bright, unshaded tracers */
    for (i = 0; i < s->n_proj; i++) {
        const SnapProj *p = &s->proj[i];
        float fy = p->y + p->vy * dsec;
        if (!p->active) continue;
        if (p->is_bomb) {
            float by_h = p->fall * mapY(p->altitude) + 3.0f;   /* arc down to ground */
            glColor3f(1.0f, 0.6f, 0.2f);
            glPushMatrix(); put(p->x, fy, by_h); box(4.5f, 4.5f, 4.5f); glPopMatrix();
        } else {
            const float *c = ALT_COLOR[p->altitude < ALT_COUNT ? p->altitude : 0];
            glColor3f(c[0], c[1], c[2]);
            glPushMatrix(); put(p->x, fy, mapY(p->altitude)); box(2.5f, 2.5f, 10.0f); glPopMatrix();
        }
    }
    glEnable(GL_LIGHTING);
}

/* Expanding burst discs on the ground where bombs detonated. */
static void draw_explosions(const RenderSnapshot *s)
{
    int i, j;
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    for (i = 0; i < s->n_expl; i++) {
        const SnapExpl *e = &s->expl[i];
        float rad = e->progress * BOMB_RADIUS;
        float a   = 1.0f - e->progress;
        float X = mapX(e->x), Z = mapZ(e->y);
        float rx = rad * F3_XSCALE, rz = rad * 0.8f;
        glColor4f(1.0f, 0.55f, 0.2f, a * 0.5f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(X, 1.0f, Z);
        for (j = 0; j <= 16; j++) {
            float ang = (float)j / 16.0f * 6.2831853f;
            glVertex3f(X + cosf(ang) * rx, 1.0f, Z + sinf(ang) * rz);
        }
        glEnd();
        glColor4f(1.0f, 0.9f, 0.5f, a);
        glBegin(GL_LINE_LOOP);
        for (j = 0; j < 24; j++) {
            float ang = (float)j / 24.0f * 6.2831853f;
            glVertex3f(X + cosf(ang) * rx, 1.2f, Z + sinf(ang) * rz);
        }
        glEnd();
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

/* Enemy death: the shape breaks into tumbling fragments that fly out and fall,
 * plus a brief core flash. Fragment directions are seeded so they're stable
 * across frames. Called with lighting ON. */
static void draw_sparks(const RenderSnapshot *s)
{
    static const float GROUND_COL[3] = { 0.85f, 0.35f, 0.30f };
    int i, j;
    for (i = 0; i < s->n_spark; i++) {
        const SnapSpark *sp = &s->spark[i];
        const float *c = (sp->etype == 2) ? GROUND_COL
                         : ALT_COLOR[sp->alt < ALT_COUNT ? sp->alt : 0];
        float p  = sp->progress;
        int   nf = (sp->etype == 1) ? 8 : 5;
        float cx = mapX(sp->x), cy = mapYf((float)sp->alt), cz = mapZ(sp->y);
        for (j = 0; j < nf; j++) {
            float h1 = hashf(sp->seed * 7u + (unsigned)j);
            float h2 = hashf(sp->seed * 13u + (unsigned)j * 5u + 3u);
            float h3 = hashf(sp->seed * 17u + (unsigned)j + 9u);
            float ang  = h1 * 6.2831853f;
            float spd  = (34.0f + 46.0f * h2) * ((sp->etype == 1) ? 1.4f : 1.0f);
            float dist = p * spd;
            float fx = cosf(ang) * dist;
            float fz = sinf(ang) * dist * 0.7f;
            float fy = (0.4f + h3) * dist * 0.5f - p * p * 46.0f;   /* rise then fall */
            float sz = ((sp->etype == 1) ? 7.0f : 5.0f) * (1.0f - p);
            if (sz < 0.6f) continue;
            glColor3f(c[0], c[1], c[2]);
            glPushMatrix();
            glTranslatef(cx + fx, cy + fy, cz + fz);
            glRotatef(p * 500.0f * (h1 - 0.5f), 1.0f, 0.6f, 0.3f);
            box(sz, sz, sz);
            glPopMatrix();
        }
        if (p < 0.45f) {                            /* core flash */
            float fa = (0.45f - p) / 0.45f;
            glDisable(GL_LIGHTING);
            glDepthMask(GL_FALSE);
            glColor4f(1.0f, 0.9f, 0.6f, fa);
            glPushMatrix();
            glTranslatef(cx, cy, cz);
            octa(6.0f + 10.0f * p);
            glPopMatrix();
            glDepthMask(GL_TRUE);
            glEnable(GL_LIGHTING);
        }
    }
}

/* Slow turret bullets skimming the ground; dodge by ascending or sidestepping. */
static void draw_tbullets(const RenderSnapshot *s, float dsec)
{
    int i;
    glDisable(GL_LIGHTING);
    for (i = 0; i < s->n_tbul; i++) {
        float y = s->tbul[i].y + TURRET_BULLET_SPEED * dsec;
        glColor3f(1.0f, 0.45f, 0.20f);
        glPushMatrix();
        put(s->tbul[i].x, y, mapY(ALT_LOW) + 6.0f);
        octa(7.0f);
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);
}

/* ---- HUD ------------------------------------------------------------------ */
static const char *ALT_NAME[ALT_COUNT] = { "LOW", "MID", "HIGH" };

static void draw_hud(const RenderSnapshot *s, int w, int h)
{
    char line[128];
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)w, 0.0, (double)h, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    glColor3f(0.95f, 0.97f, 1.0f);
    snprintf(line, sizeof line, "SCORE %07ld", (long)s->score);
    glfont_text(12.0f, (float)h - 24.0f, line);
    snprintf(line, sizeof line, "LIVES %d    HI %07ld", s->lives, (long)s->hi_score);
    glfont_text(12.0f, (float)h - 46.0f, line);

    snprintf(line, sizeof line, "%s  %s  WID %d  PWR %d  SPC %d  ALT %s",
             role_name(s->p_role), weapon_name(s->p_role, s->p_weapon),
             s->p_width, s->p_power, s->p_special,
             ALT_NAME[s->p_alt < ALT_COUNT ? s->p_alt : 0]);
    glColor3f(0.75f, 0.85f, 1.0f);
    glfont_text(12.0f, 14.0f, line);

    /* high-score board (top-right) */
    {
        int   bi;
        float yy = (float)h - 24.0f, xx = (float)w - 190.0f;
        glColor3f(0.90f, 0.85f, 0.50f);
        glfont_text(xx, yy, "-- BEST --");
        yy -= 20.0f;
        for (bi = 0; bi < s->n_board && bi < 6; bi++) {
            char row[48];
            snprintf(row, sizeof row, "%-10.10s %6ld", s->board[bi].name, (long)s->board[bi].best);
            if (s->board[bi].is_local) glColor3f(1.0f, 1.0f, 0.6f);
            else                       glColor3f(0.8f, 0.85f, 0.95f);
            glfont_text(xx, yy, row);
            yy -= 18.0f;
        }
    }

    if (s->p_is_mod) {
        glColor3f(0.45f, 0.52f, 0.62f);
        glfont_text(12.0f, 34.0f, "BACKSPACE = NEW GAME");
    }

    if (s->gameover) {
        glColor3f(1.0f, 0.5f, 0.5f);
        glfont_text((float)w * 0.5f - 60.0f, (float)h * 0.5f + 22.0f, "GAME OVER");
        glColor3f(0.9f, 0.9f, 0.9f);
        glfont_text((float)w * 0.5f - 95.0f, (float)h * 0.5f - 2.0f, "INSERT = REJOIN");
        if (s->p_is_mod) {
            glfont_text((float)w * 0.5f - 95.0f, (float)h * 0.5f - 24.0f, "BACKSPACE = NEW GAME");
        } else {
            glColor3f(0.6f, 0.6f, 0.6f);
            glfont_text((float)w * 0.5f - 95.0f, (float)h * 0.5f - 24.0f, "NEW GAME: MODERATOR ONLY");
        }
    }
    glEnable(GL_DEPTH_TEST);
}

/* ---- thread --------------------------------------------------------------- */

static void gl_setup_lighting(void)
{
    GLfloat dif[] = { 0.9f, 0.9f, 0.88f, 1.0f };
    GLfloat amb[] = { 0.38f, 0.38f, 0.44f, 1.0f };
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glEnable(GL_LIGHT0);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
}

static DWORD WINAPI render_thread(LPVOID param)
{
    HWND canvas = (HWND)param;
    HDC  dc = GetDC(s_glwnd);
    PIXELFORMATDESCRIPTOR pfd;
    int  pf, w = 0, h = 0;
    HGLRC rc;
    WorldView *wv = (WorldView *)malloc(sizeof(WorldView));

    ZeroMemory(&pfd, sizeof pfd);
    pfd.nSize = sizeof pfd; pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 24; pfd.cDepthBits = 16;
    pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd) || !(rc = wglCreateContext(dc))) {
        ReleaseDC(s_glwnd, dc); free(wv); return 1;
    }
    wglMakeCurrent(dc, rc);
    gl_setup_lighting();
    glfont_init(dc);
    timeBeginPeriod(1);

    while (s_run) {
        RECT r;
        RenderSnapshot snap;
        GLfloat lpos[] = { -0.3f, 0.85f, 0.35f, 0.0f };   /* directional */

        if (canvas && GetClientRect(canvas, &r) && (r.right != w || r.bottom != h)) {
            w = r.right; h = r.bottom;
            SetWindowPos(s_glwnd, NULL, 0, 0, w, h,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        }
        if (w <= 0 || h <= 0) { Sleep(16); continue; }

        glViewport(0, 0, w, h);
        glClearColor(0.05f, 0.06f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (wv && snap_consume(&snap)) {
            DWORD now = GetTickCount();
            long  d   = (long)(now - snap.stamp_ms);
            uint32_t rt;
            float dsec;
            if (d < 0) d = 0;
            if (d > 150) d = 150;
            rt   = snap.game_time_ms + (uint32_t)d;
            dsec = (float)d / 1000.0f;

            world_build(snap.seed, rt, wv);
            set_projection(w, h);
            camera();
            glLightfv(GL_LIGHT0, GL_POSITION, lpos);

            glDisable(GL_LIGHTING);
            draw_stars(wv->scroll);
            draw_ground(wv->scroll);
            draw_shadows(&snap, wv);
            glEnable(GL_LIGHTING);
            draw_volumes(&snap, wv, rt);
            draw_projectiles(&snap, dsec);
            draw_tbullets(&snap, dsec);
            draw_explosions(&snap);
            draw_sparks(&snap);
            draw_hud(&snap, w, h);
        }
        SwapBuffers(dc);
        Sleep(16);
    }

    timeEndPeriod(1);
    glfont_destroy();
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(s_glwnd, dc);
    free(wv);
    return 0;
}

/* ---- api ------------------------------------------------------------------ */

void render_start(HWND canvas)
{
    HINSTANCE hi = self_instance();
    WNDCLASSA wc;
    RECT rc;

    ZeroMemory(&wc, sizeof wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = gl_wndproc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = GL_CLASS;
    RegisterClassA(&wc);

    if (!canvas || !GetClientRect(canvas, &rc)) return;
    s_glwnd = CreateWindowExA(0, GL_CLASS, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                              0, 0, rc.right, rc.bottom, canvas, NULL, hi, NULL);
    if (!s_glwnd) return;

    s_run = 1;
    s_thread = CreateThread(NULL, 0, render_thread, canvas, 0, NULL);
    if (!s_thread) { s_run = 0; DestroyWindow(s_glwnd); s_glwnd = 0; }
}

void render_stop(void)
{
    s_run = 0;
    if (s_thread) { WaitForSingleObject(s_thread, 2000); CloseHandle(s_thread); s_thread = 0; }
    if (s_glwnd)  { DestroyWindow(s_glwnd); s_glwnd = 0; }
}
