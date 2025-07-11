//
// FoXBot - AI Bot for Halflife's Team Fortress Classic
//
// (http://foxbot.net)
//
// bot_navigate.h
//
// Copyright (C) 2003 - Tom "Redfox" Simpson
//
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
// See the GNU General Public License for more details at:
// http://www.gnu.org/copyleft/gpl.html
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//

#ifndef BOT_NAVIGATE_H
#define BOT_NAVIGATE_H

// standard amount of time to reach the bots current waypoint
#define BOT_WP_DEADLINE 7.0

// coarse navigation zones
enum MapZone { ZONE_UNKNOWN = 0, ZONE_BASE, ZONE_MID, ZONE_ENEMY_BASE };

void BotUpdateHomeInfo(const bot_t *pBot);

void BotFindCurrentWaypoint(bot_t *pBot);

void BotSetFacing(const bot_t *pBot, Vector v_focus);

void BotFixIdealPitch(edict_t *pEdict);

float BotChangePitch(edict_t *pEdict, float speed);

void BotFixIdealYaw(edict_t *pEdict);

float BotChangeYaw(edict_t *pEdict, float speed);

void BotNavigateWaypointless(bot_t *pBot);

bool BotNavigateWaypoints(bot_t *pBot, bool navByStrafe);

bool BotHeadTowardWaypoint(bot_t *pBot, bool &r_navByStrafe);

void BotUseLift(bot_t *pBot);

bool BotCheckWallOnLeft(const bot_t *pBot);

bool BotCheckWallOnRight(const bot_t *pBot);

int BotFindFlagWaypoint(const bot_t *pBot);

int BotFindFlagGoal(const bot_t *pBot);

int BotGoForSniperSpot(const bot_t *pBot);

int BotTargetDefenderWaypoint(const bot_t *pBot);

int BotGetDispenserBuildWaypoint(const bot_t *pBot);

int BotGetTeleporterBuildWaypoint(const bot_t *pBot, bool buildEntrance);

void BotFindSideRoute(bot_t *pBot);

bool BotPathCheck(int sourceWP, int destWP);

bool BotChangeRoute(bot_t *pBot);

bool BotSetAlternativeGoalWaypoint(bot_t *pBot, int &r_goalWP, WPT_INT32 flags);

int BotFindSuicideGoal(const bot_t *pBot);

int BotFindRetreatPoint(bot_t *pBot, int min_dist, const Vector &r_threatOrigin);

int BotFindThreatAvoidPoint(bot_t *pBot, int min_dist, const edict_t *pent);

int BotDrowningWaypointSearch(const bot_t *pBot);

bool BotFindTeleportShortCut(bot_t *pBot);

// nav mesh support
void BuildNavMesh();
bool NavMeshNavigate(bot_t *pBot, const Vector &goal);
void AddDangerSpot(const Vector &pos);
void AddAmbushSpot(const Vector &pos);
bool IsDangerSpot(const Vector &pos);
void LoadMapSpotData();
void SaveMapSpotData();
void CoverageRecord(const Vector &pos);
bool CoverageVisited(const Vector &pos);
Vector CoveragePickUnvisited(const Vector &origin);

#endif // BOT_NAVIGATE_H
