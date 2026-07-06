/*
 * echelon.c — Arcadia toy entry point and host-thread glue.
 *
 * This is the ONLY file that calls host services (ar_*). It runs the sim in
 * tick() on the host thread (so every host call stays on that thread), gathers
 * input by polling ar_key_down, broadcasts player state + heartbeats with
 * ar_send, applies incoming packets directly (packet() is a host-thread
 * callback too), and publishes a RenderSnapshot for the render thread.
 *
 * Controls (synSpace-style):
 *   arrows      move
 *   Home / End  ascend / descend altitude band
 *   Insert      fire  (also: rejoin after game over)
 *   F1..F3      select weapon
 *   1 / 2 / 3   select role (fighter / bomber / multi)
 */
#include <arcadia/toy.h>
#include <string.h>
#include "sim.h"
#include "net.h"
#include "snapshot.h"
#include "render.h"

static GameState g_game;
static int       g_open;
static uint32_t  s_last_state, s_last_hb, s_last_roster, s_last_gen;

/* ------------------------------------------------------------------ input */

/* ar_key_down polls global key state (GetAsyncKeyState), so with two Arcadia
 * instances up, both would read the keys. Only take input when OUR Arcadia
 * top-level window is the foreground one. */
static int arcadia_focused(void)
{
    HWND fg   = GetForegroundWindow();
    HWND root = GetAncestor(ar_window(), GA_ROOT);
    if (!fg || !root) return 0;
    return fg == root || GetAncestor(fg, GA_ROOT) == root;
}

static void gather_input(Input *in)
{
    memset(in, 0, sizeof *in);
    if (!arcadia_focused()) return;
    in->left    = ar_key_down(VK_LEFT)   ? 1 : 0;
    in->right   = ar_key_down(VK_RIGHT)  ? 1 : 0;
    in->up      = ar_key_down(VK_UP)     ? 1 : 0;
    in->down    = ar_key_down(VK_DOWN)   ? 1 : 0;
    in->ascend  = ar_key_down(VK_HOME)   ? 1 : 0;
    in->descend = ar_key_down(VK_END)    ? 1 : 0;
    in->fire     = ar_key_down(VK_INSERT) ? 1 : 0;
    in->special  = ar_key_down(VK_F5)     ? 1 : 0;
    in->new_game = ar_key_down(VK_BACK)   ? 1 : 0;   /* Backspace: fresh run */
    in->restart  = in->fire;

    if (ar_key_down(VK_F1)) in->weapon = 1;
    else if (ar_key_down(VK_F2)) in->weapon = 2;
    else if (ar_key_down(VK_F3)) in->weapon = 3;
    else if (ar_key_down(VK_F4)) in->weapon = 4;

    if (ar_key_down('1')) in->role_sel = 1;
    else if (ar_key_down('2')) in->role_sel = 2;
    else if (ar_key_down('3')) in->role_sel = 3;
}

/* ------------------------------------------------------------------ net out */
static void fetch_name(int id)
{
    ArPlayerInfo info;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    if (ar_get_player_info(id, &info) && info.name[0])
        sim_set_name(&g_game, id, info.name);
}

static void poll_roster(void)
{
    int ids[MAX_PLAYERS];
    int n = ar_get_player_list(MAX_PLAYERS, ids);
    int me = ar_local_player_id();
    int anchor = me, i;
    g_game.local_id = me;
    for (i = 0; i < n && i < MAX_PLAYERS; i++) {
        if (ids[i] < anchor) anchor = ids[i];
        fetch_name(ids[i]);
    }
    fetch_name(me);
    clock_set_anchor(&g_game.clock, anchor);
}

static void send_state(void)
{
    MsgState ms;
    uint8_t  buf[NET_MAX_MSG];
    int      len;
    ms.sid    = g_game.local_id;
    ms.x      = (int16_t)g_game.p.x;
    ms.y      = (int16_t)g_game.p.y;
    ms.alt    = g_game.p.altitude;
    ms.role   = g_game.p.role;
    ms.weapon = g_game.p.weapon;
    ms.width  = g_game.p.width_lvl;
    ms.power  = g_game.p.power_lvl;
    ms.flags  = g_game.p.alive ? 1 : 0;
    ms.score  = g_game.p.score;
    ms.best   = g_game.hi_score;
    len = net_pack_state(&ms, buf);
    ar_send(NET_CHANNEL, buf, len);
}

static void send_events(void)
{
    uint8_t buf[NET_MAX_MSG];
    int i;
    for (i = 0; i < g_game.n_oevents; i++) {
        MsgEvent ev;
        ev.sid    = g_game.local_id;
        ev.kind   = g_game.oevents[i].kind;
        ev.id     = g_game.oevents[i].id;
        ev.points = g_game.oevents[i].points;
        ar_send(NET_CHANNEL, buf, net_pack_event(&ev, buf));
    }
    g_game.n_oevents = 0;
}

static void send_fires(void)
{
    uint8_t buf[NET_MAX_MSG];
    int i;
    for (i = 0; i < g_game.n_ofires; i++) {
        MsgFire fr;
        fr.sid = g_game.local_id;
        fr.x   = g_game.ofires[i].x;
        fr.y   = g_game.ofires[i].y;
        fr.alt = g_game.ofires[i].alt;
        ar_send(NET_CHANNEL, buf, net_pack_fire(&fr, buf));
    }
    g_game.n_ofires = 0;
}

static const char *SND_FILE[SND_COUNT] = {
    "shoot.wav", "explode.wav", "powerup.wav", "death.wav", "oneup.wav", "bomb.wav"
};

/* Play this tick's sound cues (only for the focused instance, so two clients on
 * one desktop don't double up during testing). */
static void play_sounds(void)
{
    int i;
    if (!arcadia_focused()) { g_game.n_osounds = 0; return; }
    for (i = 0; i < g_game.n_osounds; i++)
        if (g_game.osounds[i] < SND_COUNT) ar_play_sound(SND_FILE[g_game.osounds[i]]);
    g_game.n_osounds = 0;
}

static void send_heartbeat(uint32_t now)
{
    MsgHeartbeat hb;
    uint8_t      buf[NET_MAX_MSG];
    int          len;
    hb.sid       = g_game.local_id;
    hb.seed      = g_game.clock.seed;
    hb.game_time = clock_time(&g_game.clock, now);
    hb.gen       = g_game.clock.gen;
    hb.is_anchor = (g_game.local_id == g_game.clock.anchor_id) ? 1 : 0;
    len = net_pack_heartbeat(&hb, buf);
    ar_send(NET_CHANNEL, buf, len);
}

/* ------------------------------------------------------------------ callbacks */
static int on_open(ArContext *ctx, HWND canvas, void *offer)
{
    uint32_t now = GetTickCount();
    (void)ctx; (void)offer;

    snap_init();
    sim_init(&g_game, now ^ 0x9E3779B9u, now);   /* tentative seed; adopts a peer's on join */
    s_last_state = s_last_hb = 0;
    s_last_roster = 0;
    render_start(canvas);
    g_open = 1;
    ar_print("Echelon: arrows move, Home/End altitude, Insert fires, 1/2/3 role, F1-F3 weapon.");
    return 0;
}

static void on_tick(ArContext *ctx, unsigned now_ms)
{
    Input in;
    RenderSnapshot snap;
    (void)ctx;
    if (!g_open) return;

    if (now_ms - s_last_roster >= ROSTER_POLL_MS || s_last_roster == 0) {
        poll_roster();
        s_last_roster = now_ms;
    }

    gather_input(&in);
    sim_advance(&g_game, &in, now_ms);
    play_sounds();

    /* a New Game bumps the clock generation — push it out immediately */
    if (g_game.clock.gen != s_last_gen) {
        s_last_gen = g_game.clock.gen;
        if (g_game.local_id >= 0) { send_heartbeat(now_ms); s_last_hb = now_ms; }
    }

    if (g_game.local_id >= 0) {
        send_events();     /* immediate: kills/clears/pickups */
        send_fires();      /* immediate: muzzle events for remote tracers */
        if (now_ms - s_last_state >= STATE_SEND_MS) { send_state(); s_last_state = now_ms; }
        if (now_ms - s_last_hb >= HEARTBEAT_MS)    { send_heartbeat(now_ms); s_last_hb = now_ms; }
    }

    sim_snapshot(&g_game, &snap, now_ms);
    snap_publish(&snap);
}

static void on_packet(ArContext *ctx, int channel, const void *data, int len)
{
    MsgState st; MsgFire fr; MsgEvent ev; MsgHeartbeat hb;
    uint32_t now = GetTickCount();
    int t;
    (void)ctx; (void)channel;
    if (!g_open) return;
    t = net_unpack((const uint8_t *)data, len, &st, &fr, &ev, &hb);
    switch (t) {
    case MSG_STATE:     sim_apply_state(&g_game, &st, now); break;
    case MSG_FIRE:      sim_apply_fire(&g_game, &fr, now);  break;
    case MSG_EVENT:     sim_apply_event(&g_game, &ev);      break;
    case MSG_HEARTBEAT: clock_observe(&g_game.clock, g_game.local_id, hb.sid, hb.seed,
                                      hb.game_time, hb.gen, hb.is_anchor, now); break;
    }
}

/* Late-joiner bootstrap: hand a peer our (seed, gameTime) so it can adopt the
 * shared world immediately. Encoded as a heartbeat so packet() handles it too. */
static int on_serialize(ArContext *ctx, int channel, void *buf, int cap)
{
    MsgHeartbeat hb;
    (void)ctx;
    if (channel != NET_CHANNEL || cap < NET_HEARTBEAT_BYTES || !g_open) return 0;
    hb.sid       = g_game.local_id;
    hb.seed      = g_game.clock.seed;
    hb.game_time = clock_time(&g_game.clock, GetTickCount());
    hb.gen       = g_game.clock.gen;
    hb.is_anchor = (g_game.local_id == g_game.clock.anchor_id) ? 1 : 0;
    return net_pack_heartbeat(&hb, (uint8_t *)buf);
}

static void on_close(ArContext *ctx)
{
    (void)ctx;
    g_open = 0;
    render_stop();
    snap_destroy();
}

void ArcadiaToyRegister(ArToy *toy)
{
    toy->name      = "Echelon";
    toy->open      = on_open;
    toy->close     = on_close;
    toy->tick      = on_tick;
    toy->packet    = on_packet;
    toy->serialize = on_serialize;
}
