/*
 * OpenTech2 test mod: shared between the weapon (server side) and its
 * client effect.
 */

#ifndef GAME_TESTMOD_H
#define GAME_TESTMOD_H

/* temporary entity types of this mod: above the stock ones (0..~100) */
#define TE_TESTGUN_BEAM 201 /* [start position] [end position] */

void Weapon_TestGun(edict_t *ent);

#endif
