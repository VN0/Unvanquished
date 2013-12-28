/*
===========================================================================

Daemon GPL Source Code
Copyright (C) 2012 Unvanquished Developers

This file is part of the Daemon GPL Source Code (Daemon Source Code).

Daemon Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Daemon Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Daemon Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following the
terms and conditions of the GNU General Public License which accompanied the Daemon
Source Code.  If not, please request a copy in writing from id Software at the address
below.

If you have questions concerning this license or the applicable additional terms, you
may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville,
Maryland 20850 USA.

===========================================================================
*/

#include "cg_local.h"

rocketInfo_t rocketInfo;

vmCvar_t rocket_menuFile;
vmCvar_t rocket_hudFile;

typedef struct
{
	vmCvar_t   *vmCvar;
	const char *cvarName;
	const char *defaultString;
	int        cvarFlags;
} cvarTable_t;

static const cvarTable_t rocketCvarTable[] =
{
	{ &rocket_hudFile, "rocket_hudFile", "ui/rockethud.txt", CVAR_ARCHIVE },
	{ &rocket_menuFile, "rocket_menuFile", "ui/rocket.txt", CVAR_ARCHIVE }
};

static const size_t rocketCvarTableSize = ARRAY_LEN( rocketCvarTable );

/*
=================
CG_RegisterRocketCvars
=================
*/
void CG_RegisterRocketCvars( void )
{
	int         i;
	const cvarTable_t *cv;

	for ( i = 0, cv = rocketCvarTable; i < rocketCvarTableSize; i++, cv++ )
	{
		trap_Cvar_Register( cv->vmCvar, cv->cvarName,
		                    cv->defaultString, cv->cvarFlags );
	}
}

static rocketState_t oldRocketState;
static connstate_t oldConnState;

void CG_Rocket_Init( void )
{
	int len;
	char *token, *text_p;
	char text[ 20000 ];
	fileHandle_t f;

	oldConnState = CA_UNINITIALIZED;
	oldRocketState = IDLE;

	// Version check...
	trap_SyscallABIVersion( SYSCALL_ABI_VERSION_MAJOR, SYSCALL_ABI_VERSION_MINOR );

	// Init Rocket
	trap_Rocket_Init();

	// Dynamic memory
	BG_InitMemory();


	// rocket cvars
	CG_RegisterRocketCvars();


	// Intialize data sources...
	CG_Rocket_RegisterDataSources();
	CG_Rocket_RegisterDataFormatters();

	// Register elements
	CG_Rocket_RegisterElements();

	rocketInfo.rocketState = IDLE;

	// Preload all the menu files...
	len = trap_FS_FOpenFile( rocket_menuFile.string, &f, FS_READ );

	if ( len <= 0 )
	{
		Com_Error( ERR_DROP, "Unable to load %s. No rocket menus loaded.", rocket_menuFile.string );
	}

	if ( len >= sizeof( text ) - 1 )
	{
		trap_FS_FCloseFile( f );
		Com_Error( ERR_DROP, "File %s too long.", rocket_menuFile.string );
	}

	trap_FS_Read( text, len, f );
	text[ len ] = 0;
	text_p = text;
	trap_FS_FCloseFile( f );

	// Parse files to load...
	while ( 1 )
	{
		token = COM_Parse2( &text_p );

		// Closing bracket. EOF
		if ( !*token || *token == '}' )
		{
			break;
		}

		// Ignore opening bracket
		if ( *token == '{' )
		{
			continue;
		}

		// Set the cursor
		if ( !Q_stricmp( token, "cursor" ) )
		{
			token = COM_Parse2( &text_p );

			// Skip non-RML files
			if ( Q_stricmp( token + strlen( token ) - 4, ".rml" ) )
			{
				continue;
			}

			trap_Rocket_LoadCursor( token );
			continue;
		}

		if ( !Q_stricmp( token, "main" ) )
		{
			int i;

			token = COM_Parse2( &text_p );

			if ( *token != '{' )
			{
				Com_Error( ERR_DROP, "Error parsing %s. Expecting \"{\" but found \"%c\".", rocket_menuFile.string, *token );
			}

			for ( i = 0; i < ROCKETMENU_NUM_TYPES; ++i )
			{
				token = COM_Parse2( &text_p );

				if ( !*token )
				{
					Com_Error( ERR_DROP, "Error parsing %s. Unexpected end of file. Expecting path to RML menu.", rocket_menuFile.string );
				}

				rocketInfo.menu[ i ].path = BG_strdup( token );
				trap_Rocket_LoadDocument( token );

				token = COM_Parse2( &text_p );

				if ( !*token )
				{
					Com_Error( ERR_DROP, "Error parsing %s. Unexpected end of file. Expecting RML document id.", rocket_menuFile.string );
				}

				rocketInfo.menu[ i ].id = BG_strdup( token );
			}

			token = COM_Parse2( &text_p );

			if ( *token != '}' )
			{
				Com_Error( ERR_DROP, "Error parsing %s. Expecting \"}\" but found \"%c\".", rocket_menuFile.string, *token );
			}

			while ( *token && *token != '}' )
			{
				token = COM_Parse2( &text_p );

				if ( !*token )
				{
					Com_Error( ERR_DROP, "Error parsing %s. Unexpected end of file. Expecting RML document.", rocket_menuFile.string );
				}

				trap_Rocket_LoadDocument( token );
			}

			continue;
		}

		if ( !Q_stricmp( token, "misc" ) )
		{
			token = COM_Parse2( &text_p );

			if ( *token != '{' )
			{
				Com_Error( ERR_DROP, "Error parsing %s. Expecting \"{\" but found \"%c\".", rocket_menuFile.string, *token );
			}

			while ( 1 )
			{
				token = COM_Parse2( &text_p );

				if ( *token == '}' )
				{
					break;
				}

				if ( !*token )
				{
					Com_Error( ERR_DROP, "Error parsing %s. Unexpected end of file. Expecting closing '}'.", rocket_menuFile.string );
				}

				// Skip non-RML files
				if ( Q_stricmp( token + strlen( token ) - 4, ".rml" ) )
				{
					Com_Printf( "^3WARNING: Non-RML file listed in %s: \"%s\" . Skipping.", rocket_menuFile.string, token );
					continue;
				}

				trap_Rocket_LoadDocument( token );

			}

			continue;
		}
	}

	trap_Rocket_DocumentAction( rocketInfo.menu[ ROCKETMENU_MAIN ].id, "open" );
	trap_Key_SetCatcher( KEYCATCH_UI );
}

void CG_Rocket_LoadHuds( void )
{
	int i, len;
	char *token, *text_p;
	char text[ 20000 ];
	fileHandle_t f;

	// Preload all the menu files...
	len = trap_FS_FOpenFile( rocket_hudFile.string, &f, FS_READ );

	if ( len <= 0 )
	{
		Com_Error( ERR_DROP, "Unable to load %s. No rocket menus loaded.", rocket_menuFile.string );
	}

	if ( len >= sizeof( text ) - 1 )
	{
		trap_FS_FCloseFile( f );
		Com_Error( ERR_DROP, "File %s too long.", rocket_hudFile.string );
	}

	trap_FS_Read( text, len, f );
	text[ len ] = 0;
	text_p = text;
	trap_FS_FCloseFile( f );

	trap_Rocket_InitializeHuds( WP_NUM_WEAPONS );

	// Parse files to load...

	while ( 1 )
	{
		token = COM_Parse2( &text_p );

		if ( !*token )
		{
			break;
		}

		if ( !Q_stricmp( token, "units" ) )
		{
			while ( 1 )
			{
				token = COM_Parse2( &text_p );

				if ( !*token )
				{
					Com_Error( ERR_DROP, "Unable to load huds from %s. Unexpected end of file. Expected closing } to close off units.", rocket_hudFile.string );
				}

				if ( *token == '}' )
				{
					break;
				}

				// Skip non-RML files
				if ( Q_stricmp( token + strlen( token ) - 4, ".rml" ) )
				{
					continue;
				}

				trap_Rocket_LoadUnit( token );
			}

			continue;
		}

		if ( !Q_stricmp( token, "human_hud" ) )
		{
			// Clear old values
			for ( i = WP_BLASTER; i < WP_LUCIFER_CANNON; ++i )
			{
				trap_Rocket_ClearHud( i );
			}

			trap_Rocket_ClearHud( WP_HBUILD );

			while ( 1 )
			{
				token = COM_Parse2( &text_p );

				if ( !*token )
				{
					Com_Error( ERR_DROP, "Unable to load huds from %s. Unexpected end of file. Expected closing } to close off human_hud.", rocket_hudFile.string );
				}

				if ( *token == '{' )
				{
					continue;
				}

				if ( *token == '}' )
				{
					break;
				}


				for ( i = WP_BLASTER; i < WP_LUCIFER_CANNON; ++i )
				{
					trap_Rocket_AddUnitToHud( i, token );
				}

				trap_Rocket_AddUnitToHud( WP_HBUILD, token );
			}


			continue;
		}

		if ( !Q_stricmp( token, "spectator_hud" ) )
		{
			for ( i = WP_NONE; i < WP_NUM_WEAPONS; ++i )
			{
				trap_Rocket_ClearHud( i );
			}

			while ( 1 )
			{
				token = COM_Parse2( &text_p );

				if ( !*token )
				{
					Com_Error( ERR_DROP, "Unable to load huds from %s. Unexpected end of file. Expected closing } to close off spectator_hud.", rocket_hudFile.string );
				}

				if ( *token == '{' )
				{
					continue;
				}

				if ( *token == '}' )
				{
					break;
				}


				for ( i = WP_NONE; i < WP_NUM_WEAPONS; ++i )
				{
					trap_Rocket_AddUnitToHud( i, token );
				}
			}

			continue;
		}

		if ( !Q_stricmp( token, "alien_hud" ) )
		{
			for ( i = WP_ALEVEL0; i < WP_ALEVEL4; ++i )
			{
				trap_Rocket_ClearHud( i );
			}

			trap_Rocket_ClearHud( WP_ABUILD );
			trap_Rocket_ClearHud( WP_ABUILD2 );

			while ( 1 )
			{
				token = COM_Parse2( &text_p );

				if ( !*token )
				{
					Com_Error( ERR_DROP, "Unable to load huds from %s. Unexpected end of file. Expected closing } to close off alien_hud.", rocket_hudFile.string );
				}

				if ( *token == '{' )
					{
						continue;
					}

					if ( *token == '}' )
					{
						break;
					}
				for ( i = WP_ALEVEL0; i < WP_ALEVEL4; ++i )
				{
					trap_Rocket_AddUnitToHud( i, token );
				}

				trap_Rocket_AddUnitToHud( WP_ABUILD, token );
				trap_Rocket_AddUnitToHud( WP_ABUILD2, token );
			}

			continue;
		}

		for ( i = WP_NONE + 1; i < WP_NUM_WEAPONS; ++i )
		{
			if ( !Q_stricmp( token, va( "%s_hud", BG_Weapon( i )->name ) ) )
			{
				trap_Rocket_ClearHud( i );
				while ( 1 )
				{
					token = COM_Parse2( &text_p );

					if ( !*token )
					{
						Com_Error( ERR_DROP, "Unable to load huds from %s. Unexpected end of file. Expected closing } to close off spectator_hud.", rocket_hudFile.string );
					}

					if ( *token == '{' )
					{
						continue;
					}

					if ( *token == '}' )
					{
						break;
					}

					trap_Rocket_AddUnitToHud( i, token );
				}
			}
		}
	}
}

int CG_StringToNetSource( const char *src )
{
	if ( !Q_stricmp( src, "local" ) )
	{
		return AS_LOCAL;
	}

	else if ( !Q_stricmp( src, "favorites" ) )
	{
		return AS_FAVORITES;
	}

	else
	{
		return AS_GLOBAL;
	}
}

const char *CG_NetSourceToString( int netSrc )
{
	switch ( netSrc )
	{
		case AS_LOCAL:
			return "local";

		case AS_FAVORITES:
			return "favorites";

		default:
			return "internet";
	}
}


void CG_Rocket_Frame( void )
{
	trap_GetClientState( &rocketInfo.cstate );

	if ( oldConnState != rocketInfo.cstate.connState )
	{
		switch ( rocketInfo.cstate.connState )
		{
			case CA_DISCONNECTED:

				if ( rocketInfo.rocketState > BUILDING_SERVER_INFO )
				{
					rocketInfo.rocketState = IDLE;
				}

				break;

			case CA_CONNECTING:
			case CA_CHALLENGING:
			case CA_CONNECTED:
				rocketInfo.rocketState = CONNECTING;
				break;

			case CA_LOADING:
			case CA_PRIMED:
				rocketInfo.rocketState = LOADING;
				break;

			case CA_ACTIVE:
			default:
				rocketInfo.rocketState = PLAYING;
		}

		oldConnState = rocketInfo.cstate.connState;
	}

	if ( oldRocketState != rocketInfo.rocketState )
	{
		switch ( rocketInfo.rocketState )
		{
			case RETRIEVING_SERVERS:
				if ( trap_LAN_UpdateVisiblePings( rocketInfo.currentNetSrc ) )
				{
				}
				else
				{
					rocketInfo.rocketState = IDLE;
				}

				break;

			case BUILDING_SERVER_INFO:
				CG_Rocket_BuildServerInfo();
				break;

			case CONNECTING:
				trap_Rocket_DocumentAction( "", "blurall" );
				trap_Rocket_DocumentAction( rocketInfo.menu[ ROCKETMENU_CONNECTING ].id, "show" );

			case LOADING:
				CG_Rocket_CleanUpServerList( NULL );
				trap_Rocket_DocumentAction( "", "blurall" );
				trap_Rocket_DocumentAction( rocketInfo.menu[ ROCKETMENU_LOADING ].id, "show" );
				break;

			case PLAYING:
				trap_Rocket_DocumentAction( rocketInfo.menu[ ROCKETMENU_CONNECTING ].id, "blurall" );
				break;
		}

		oldRocketState = rocketInfo.rocketState;
	}

	CG_Rocket_ProcessEvents();
}

const char *CG_Rocket_GetTag()
{
	static char tag[ 100 ];

	trap_Rocket_GetElementTag( tag, sizeof( tag ) );

	return tag;
}

const char *CG_Rocket_GetAttribute( const char *attribute )
{
	static char buffer[ 1000 ];

	trap_Rocket_GetAttribute( attribute, buffer, sizeof( buffer ) );

	return buffer;
}

const char *CG_Rocket_QuakeToRML( const char *in )
{
	static char buffer[ MAX_STRING_CHARS ];
	trap_Rocket_QuakeToRML( in, buffer, sizeof( buffer ) );
	return buffer;
}

qboolean CG_Rocket_IsCommandAllowed( rocketElementType_t type )
{
	playerState_t *ps = &cg.predictedPlayerState;

	switch ( type )
	{
		case ELEMENT_ALL:
			return qtrue;

		case ELEMENT_LOADING:
			if ( rocketInfo.rocketState == LOADING )
			{
				return qtrue;
			}

			return qfalse;

		case ELEMENT_GAME:
			if ( rocketInfo.rocketState == PLAYING )
			{
				return qtrue;
			}

			return qfalse;

		case ELEMENT_ALIENS:
			if ( ps->persistant[ PERS_TEAM ] == TEAM_ALIENS && ps->stats[ STAT_HEALTH ] > 0 && ps->weapon != WP_NONE )
			{
				return qtrue;
			}

			return qfalse;

		case ELEMENT_HUMANS:
			if ( ps->persistant[ PERS_TEAM ] == TEAM_HUMANS && ps->stats[ STAT_HEALTH ] > 0 && ps->weapon != WP_NONE )
			{
				return qtrue;
			}

			return qfalse;

		case ELEMENT_BOTH:
			if ( ps->persistant[ PERS_TEAM ] != TEAM_NONE && ps->stats[ STAT_HEALTH ] > 0 && ps->weapon != WP_NONE )
			{
				return qtrue;
			}

			return qfalse;

		case ELEMENT_DEAD:
			if ( ps->persistant[ PERS_TEAM ] != TEAM_NONE && ps->stats[ STAT_HEALTH ] == 0 && ps->weapon == WP_NONE )
			{
				return qtrue;
			}

			return qfalse;
	}

	return qfalse;
}
