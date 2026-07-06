/*
 * roles.h — role / weapon / powerup behavior, data-driven.
 *
 * Two orthogonal upgrade axes: WIDTH (dispersion, more projectiles) and POWER
 * (strength, more damage). Fighter is air-only, Bomber ground-only, Multi both
 * (weaker). Specials (F4/F5) land in Phase 3; here F1..F3 cover the core.
 */
#ifndef ECHELON_ROLES_H
#define ECHELON_ROLES_H

#include "types.h"

enum { ROLE_FIGHTER = 0, ROLE_BOMBER = 1, ROLE_MULTI = 2, ROLE_COUNT = 3 };

const char *role_name(int role);
int  role_num_weapons(int role);
const char *weapon_name(int role, int weapon);

/* Emit projectiles for one shot into out[0..cap). Returns the fire cooldown in
 * ms (caller enforces it), or 0 if nothing was fired. *out_count gets the number
 * of projectiles appended. (x,y) is the player muzzle, alt its altitude band. */
int role_fire(int role, int weapon, int width, int power,
              float x, float y, int alt, int owner,
              Proj *out, int cap, int *out_count);

/* Fire the role's special (F5): fighter = wide air burst, bomber = wide-radius
 * terrain-clear bomb. Returns the cooldown ms; fills out[0..*out_count). */
int role_special(int role, int width, int power, float x, float y, int alt,
                 int owner, Proj *out, int cap, int *out_count);

#endif /* ECHELON_ROLES_H */
