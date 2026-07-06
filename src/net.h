/*
 * net.h — Echelon wire protocol (pure pack/unpack, no host calls).
 *
 * Every message is little-endian with a 1-byte type followed by the sender's
 * player id (attribution; the SDK packet() callback doesn't surface the sender).
 * echelon.c does the actual ar_send()/packet() plumbing on the host thread.
 *
 * Phase 2 messages: PLAYER_STATE (see remote ships) and HEARTBEAT (seed +
 * gameTime for clock consensus / late-join adoption). KILL/CLEARED/TAKEN/score
 * gossip arrive in Phase 3.
 */
#ifndef ECHELON_NET_H
#define ECHELON_NET_H

#include <stdint.h>

enum { MSG_STATE = 0x01, MSG_FIRE = 0x02, MSG_EVENT = 0x03, MSG_HEARTBEAT = 0x06 };

/* Event kinds carried by MSG_EVENT (client-authoritative co-op sync). */
enum { EV_KILL = 0, EV_CLEARED = 1, EV_TAKEN = 2 };

#define NET_STATE_BYTES     23
#define NET_FIRE_BYTES      10
#define NET_EVENT_BYTES     14
#define NET_HEARTBEAT_BYTES 14
#define NET_MAX_MSG         32

typedef struct {
    int32_t sid;
    int16_t x, y;             /* field coords 0..FIELD_W / 0..FIELD_H */
    uint8_t alt, role, weapon, width, power, flags;   /* flags bit0 = alive */
    int32_t score;
    int32_t best;             /* this player's session best (for the board) */
} MsgState;

typedef struct {
    int32_t sid;
    int16_t x, y;
    uint8_t alt;
} MsgFire;

typedef struct {
    int32_t  sid;
    uint8_t  kind;            /* EV_* */
    uint32_t id;             /* deterministic entity id */
    int32_t  points;          /* kill score value (0 for others) */
} MsgEvent;

typedef struct {
    int32_t  sid;
    uint32_t seed;
    uint32_t game_time;
    uint8_t  is_anchor;
} MsgHeartbeat;

int net_pack_state(const MsgState *m, uint8_t *buf);         /* -> bytes written */
int net_pack_fire(const MsgFire *m, uint8_t *buf);
int net_pack_event(const MsgEvent *m, uint8_t *buf);
int net_pack_heartbeat(const MsgHeartbeat *m, uint8_t *buf);

/* Decode one message. Returns the MSG_* type (and fills the matching struct via
 * the right out-param), or 0 if unrecognized / too short. */
int net_unpack(const uint8_t *data, int len, MsgState *st, MsgFire *fr,
               MsgEvent *ev, MsgHeartbeat *hb);

#endif /* ECHELON_NET_H */
