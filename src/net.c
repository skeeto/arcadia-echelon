#include "net.h"

/* ---- little-endian primitives -------------------------------------------- */

static int put_u8 (uint8_t *b, int o, uint8_t v)  { b[o] = v; return o + 1; }
static int put_u16(uint8_t *b, int o, uint16_t v) { b[o] = (uint8_t)v; b[o+1] = (uint8_t)(v >> 8); return o + 2; }
static int put_u32(uint8_t *b, int o, uint32_t v)
{
    b[o]   = (uint8_t)v;         b[o+1] = (uint8_t)(v >> 8);
    b[o+2] = (uint8_t)(v >> 16); b[o+3] = (uint8_t)(v >> 24);
    return o + 4;
}

static uint8_t  get_u8 (const uint8_t *b, int o) { return b[o]; }
static uint16_t get_u16(const uint8_t *b, int o) { return (uint16_t)(b[o] | (b[o+1] << 8)); }
static uint32_t get_u32(const uint8_t *b, int o)
{
    return (uint32_t)b[o] | ((uint32_t)b[o+1] << 8) |
           ((uint32_t)b[o+2] << 16) | ((uint32_t)b[o+3] << 24);
}

/* ---- pack ----------------------------------------------------------------- */

int net_pack_state(const MsgState *m, uint8_t *buf)
{
    int o = 0;
    o = put_u8 (buf, o, MSG_STATE);
    o = put_u32(buf, o, (uint32_t)m->sid);
    o = put_u16(buf, o, (uint16_t)m->x);
    o = put_u16(buf, o, (uint16_t)m->y);
    o = put_u8 (buf, o, m->alt);
    o = put_u8 (buf, o, m->role);
    o = put_u8 (buf, o, m->weapon);
    o = put_u8 (buf, o, m->width);
    o = put_u8 (buf, o, m->power);
    o = put_u8 (buf, o, m->flags);
    o = put_u32(buf, o, (uint32_t)m->score);
    o = put_u32(buf, o, (uint32_t)m->best);
    return o;   /* NET_STATE_BYTES */
}

int net_pack_fire(const MsgFire *m, uint8_t *buf)
{
    int o = 0;
    o = put_u8 (buf, o, MSG_FIRE);
    o = put_u32(buf, o, (uint32_t)m->sid);
    o = put_u16(buf, o, (uint16_t)m->x);
    o = put_u16(buf, o, (uint16_t)m->y);
    o = put_u8 (buf, o, m->alt);
    return o;   /* NET_FIRE_BYTES */
}

int net_pack_event(const MsgEvent *m, uint8_t *buf)
{
    int o = 0;
    o = put_u8 (buf, o, MSG_EVENT);
    o = put_u32(buf, o, (uint32_t)m->sid);
    o = put_u8 (buf, o, m->kind);
    o = put_u32(buf, o, m->id);
    o = put_u32(buf, o, (uint32_t)m->points);
    return o;   /* NET_EVENT_BYTES */
}

int net_pack_heartbeat(const MsgHeartbeat *m, uint8_t *buf)
{
    int o = 0;
    o = put_u8 (buf, o, MSG_HEARTBEAT);
    o = put_u32(buf, o, (uint32_t)m->sid);
    o = put_u32(buf, o, m->seed);
    o = put_u32(buf, o, m->game_time);
    o = put_u32(buf, o, m->gen);
    o = put_u8 (buf, o, m->is_anchor);
    return o;   /* NET_HEARTBEAT_BYTES */
}

/* ---- unpack --------------------------------------------------------------- */

int net_unpack(const uint8_t *data, int len, MsgState *st, MsgFire *fr,
               MsgEvent *ev, MsgHeartbeat *hb)
{
    if (len < 1) return 0;
    switch (data[0]) {
    case MSG_STATE:
        if (len < NET_STATE_BYTES) return 0;
        st->sid    = (int32_t)get_u32(data, 1);
        st->x      = (int16_t)get_u16(data, 5);
        st->y      = (int16_t)get_u16(data, 7);
        st->alt    = get_u8(data, 9);
        st->role   = get_u8(data, 10);
        st->weapon = get_u8(data, 11);
        st->width  = get_u8(data, 12);
        st->power  = get_u8(data, 13);
        st->flags  = get_u8(data, 14);
        st->score  = (int32_t)get_u32(data, 15);
        st->best   = (int32_t)get_u32(data, 19);
        return MSG_STATE;
    case MSG_FIRE:
        if (len < NET_FIRE_BYTES) return 0;
        fr->sid = (int32_t)get_u32(data, 1);
        fr->x   = (int16_t)get_u16(data, 5);
        fr->y   = (int16_t)get_u16(data, 7);
        fr->alt = get_u8(data, 9);
        return MSG_FIRE;
    case MSG_EVENT:
        if (len < NET_EVENT_BYTES) return 0;
        ev->sid    = (int32_t)get_u32(data, 1);
        ev->kind   = get_u8(data, 5);
        ev->id     = get_u32(data, 6);
        ev->points = (int32_t)get_u32(data, 10);
        return MSG_EVENT;
    case MSG_HEARTBEAT:
        if (len < NET_HEARTBEAT_BYTES) return 0;
        hb->sid       = (int32_t)get_u32(data, 1);
        hb->seed      = get_u32(data, 5);
        hb->game_time = get_u32(data, 9);
        hb->gen       = get_u32(data, 13);
        hb->is_anchor = get_u8(data, 17);
        return MSG_HEARTBEAT;
    default:
        return 0;
    }
}
