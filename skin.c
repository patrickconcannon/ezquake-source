/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

	$Id: skin.c,v 1.10 2007-03-27 21:20:52 qqshka Exp $
*/

#include "quakedef.h"
#ifdef GLQUAKE
#include "gl_model.h"
#include "gl_local.h"
#else
#include "r_model.h"
#include "r_local.h"
#endif
#include "teamplay.h"
#include "image.h"


qbool OnChangeSkinForcing(cvar_t *var, char *string);

cvar_t	baseskin = {"baseskin", "base"};
cvar_t	noskins = {"noskins", "0"};

cvar_t	cl_name_as_skin = {"cl_name_as_skin", "0", 0, OnChangeSkinForcing};

char	allskins[MAX_OSPATH];

#define	MAX_CACHED_SKINS	128
skin_t	skins[MAX_CACHED_SKINS];

int		numskins;


// get player skin as player name or player id
char *Skin_AsNameOrId (player_info_t *sc) {
	static char name[MAX_OSPATH];

	if (!cls.demoplayback && !cl.spectator)
		return NULL; // allow in demos or for specs

	if (!cl_name_as_skin.value)
		return NULL;

	if (cl_name_as_skin.value == 1) { // get skin as player name
		char *s = sc->name;
		int i;

		for (i = 0; *s && i < sizeof(name) - 1; s++) {
			name[i] = s[0] & 127; // strip high bit

			if ((unsigned char)name[i] < 32 
				|| name[i] == '?' || name[i] == '*' || name[i] == ':' || name[i] == '<' || name[i] == '>' || name[i] == '"'
			   )
				name[i] = '_'; // u can't use skin with such chars, so replace with some safe char

			i++;
		}
		name[i] = 0;
		return name;
	}

	// get skin as id
	snprintf(name, sizeof(name), "%d", sc->userid);
	return name;
}

char *Skin_FindName (player_info_t *sc) {
	int tracknum;
	static char name[MAX_OSPATH];

	if (allskins[0]) {
		strlcpy(name, allskins, sizeof(name));
	} else {
		char *s = Skin_AsNameOrId(sc);

		if (!s || !s[0])
			s = Info_ValueForKey(sc->userinfo, "skin");

		if (s && s[0])
			strlcpy(name, s, sizeof(name));
		else
			strlcpy(name, baseskin.string, sizeof(name));
	}

	if (cl.spectator && (tracknum = Cam_TrackNum()) != -1)
		skinforcing_team = cl.players[tracknum].team;
	else if (!cl.spectator)
		skinforcing_team = cl.players[cl.playernum].team;

	if (!cl.teamfortress && !(cl.fpd & FPD_NO_FORCE_SKIN)) {
		char *skinname = NULL;
		player_state_t *state;
		qbool teammate;

		teammate = (cl.teamplay && !strcmp(sc->team, skinforcing_team)) ? true : false;

		if (!cl.validsequence)
			goto nopowerups;

		state = cl.frames[cl.parsecount & UPDATE_MASK].playerstate + (sc - cl.players);

		if (state->messagenum != cl.parsecount)
			goto nopowerups;

		if ((state->effects & (EF_BLUE | EF_RED)) == (EF_BLUE | EF_RED) )
			skinname = teammate ? cl_teambothskin.string : cl_enemybothskin.string;
		else if (state->effects & EF_BLUE)
			skinname = teammate ? cl_teamquadskin.string : cl_enemyquadskin.string;
		else if (state->effects & EF_RED)
			skinname = teammate ? cl_teampentskin.string : cl_enemypentskin.string;

	nopowerups:
		if (!skinname || !skinname[0])
			skinname = teammate ? cl_teamskin.string : cl_enemyskin.string;
		if (skinname[0])
			strlcpy(name, skinname, sizeof(name));
	}

	if (strstr(name, "..") || *name == '.')
		strlcpy(name, baseskin.string, sizeof(name));

	return name;
}

//Determines the best skin for the given scoreboard slot, and sets scoreboard->skin
void Skin_Find (player_info_t *sc) {
	skin_t *skin;
	int i;
	char name[MAX_OSPATH];

	strlcpy(name, Skin_FindName(sc), sizeof(name));
	COM_StripExtension(name, name);

	for (i = 0; i < numskins; i++) {
		if (!strcmp(name, skins[i].name)) {
			sc->skin = &skins[i];
			Skin_Cache(sc->skin);
			return;
		}
	}

	if (numskins == MAX_CACHED_SKINS) {	// ran out of spots, so flush everything
		Skin_Skins_f();
		return;
	}

	skin = &skins[numskins];
	sc->skin = skin;
	numskins++;

	memset (skin, 0, sizeof(*skin));
	strlcpy(skin->name, name, sizeof(skin->name));
}

byte *Skin_PixelsLoad(char *name, int *max_w, int *max_h, int *bpp)
{
	byte *pic;

	*max_w = *max_h = *bpp = 0;

#ifdef GLQUAKE
	if ((pic = GL_LoadImagePixels (name, 0, 0, 0))) {
		// no limit in gl
		*max_w	= image_width;
		*max_h	= image_height;
		*bpp	= 4; // 32 bit

		return pic;
	}
#endif

	if ((pic = Image_LoadPCX (NULL, name, 0, 0))) {
		// pcx is limited
		*max_w	= 320;
		*max_h	= 200;
		*bpp	= 1; // 8 bit

		return pic;
	}

	return NULL;
}

//Returns a pointer to the skin bitmap, or NULL to use the default
byte *Skin_Cache (skin_t *skin) {
	int y, max_w, max_h, bpp;
	byte *pic = NULL, *out, *pix;
	char name[MAX_OSPATH];

	if (cls.downloadtype == dl_skin)
		return NULL;		// use base until downloaded
	if (noskins.value == 1) // JACK: So NOSKINS > 1 will show skins, but
		return NULL;		// not download new ones.
	if (skin->failedload)
		return NULL;

	if ((out = (byte *) Cache_Check (&skin->cache)))
		return out;

	// not cached, load from HDD

	snprintf (name, sizeof(name), "skins/%s.pcx", skin->name);

	if (!(pic = Skin_PixelsLoad(name, &max_w, &max_h, &bpp)) || image_width > max_w || image_height > max_h) {
		Com_Printf ("Couldn't load skin %s\n", name);
		Q_free(pic);
	}

	if (!pic) { // attempt load at least default/base
		snprintf (name, sizeof(name), "skins/%s.pcx", baseskin.string);

		if (!(pic = Skin_PixelsLoad(name, &max_w, &max_h, &bpp)) || image_width > max_w || image_height > max_h) {
//			Com_Printf ("Couldn't load skin %s\n", name);
			Q_free(pic);
			skin->failedload = true;
			return NULL;
		}
	}

	if (!(out = pix = (byte *) Cache_Alloc (&skin->cache, max_w * max_h * bpp, skin->name)))
		Sys_Error ("Skin_Cache: couldn't allocate");

	memset (out, 0, max_w * max_h * bpp);
	for (y = 0; y < image_height; y++, pix += (max_w * bpp))
		memcpy (pix, pic + y * image_width * bpp, image_width * bpp);

	Q_free (pic);
#ifdef GLQUAKE
	skin->bpp 		 = bpp;
	skin->width		 = image_width;
	skin->height	 = image_height;
#endif
	skin->failedload = false;

	return out;
}
void Skin_NextDownload (void) {
	player_info_t *sc;
	int i;

	if (cls.downloadnumber == 0) {
		Com_Printf ("Checking skins...\n");
	}

	cls.downloadtype = dl_skin;

	for ( ; cls.downloadnumber != MAX_CLIENTS; cls.downloadnumber++) {
		sc = &cl.players[cls.downloadnumber];
		if (!sc->name[0])
			continue;
		Skin_Find (sc);
		if (noskins.value)
			continue;
		if (!CL_CheckOrDownloadFile(va("skins/%s.pcx", sc->skin->name)))
			return;		// started a download
	}

	cls.downloadtype = dl_none;

	// now load them in for real
	for (i = 0; i < MAX_CLIENTS; i++) {
		sc = &cl.players[i];
		if (!sc->name[0])
			continue;
		Skin_Cache (sc->skin);
		sc->skin = NULL;
	}

	if (cls.state == ca_onserver && cbuf_current != &cbuf_main) {	//only download when connecting
		MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
		MSG_WriteString (&cls.netchan.message, va("begin %i", cl.servercount));
		Cache_Report ();		// print remaining memory
	}
}

//Refind all skins, downloading if needed.
void Skin_Skins_f (void) {
	int i;

	for (i = 0; i < numskins; i++) {
		if (skins[i].cache.data)
			Cache_Free (&skins[i].cache);
	}
	numskins = 0;

	cls.downloadnumber = 0;
	cls.downloadtype = dl_skin;
	Skin_NextDownload ();
}

//Sets all skins to one specific one
void Skin_AllSkins_f (void) {
	if (Cmd_Argc() == 1) {
		Com_Printf("allskins set to \"%s\"\n", allskins);
		return;
	}
	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: %s [skin]\n", Cmd_Argv(0));
		return;
	}
	strlcpy (allskins, Cmd_Argv(1), sizeof(allskins));
	Skin_Skins_f();
}

//Just show skins which ezquake assign to each player, that depends on alot of variables and different conditions,
//so may be useful for checking settings
void Skin_ShowSkins_f (void) {
	int i, count, maxlen;

	maxlen = sizeof("name") - 1;

	for (i = 0; i < MAX_CLIENTS; i++)
		if (cl.players[i].name[0] && !cl.players[i].spectator)
			maxlen = bound(maxlen, strlen(cl.players[i].name), 17); // get len of longest name, but no more than 17

	for (i = count = 0; i < MAX_CLIENTS; i++)
		if (cl.players[i].name[0] && !cl.players[i].spectator) {
			if (!count)
				Com_Printf("\x02%-*.*s %s\n", maxlen, maxlen, "name", "skin");

			Com_Printf("%-*.*s %s\n", maxlen, maxlen, cl.players[i].name, Skin_FindName(&cl.players[i]));

			count++;
		}
}