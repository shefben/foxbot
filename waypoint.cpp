//
// FoXBot - AI Bot for Halflife's Team Fortress Classic
//
// (http://foxbot.net)
//
// waypoint.cpp
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

#ifndef __linux__
#include <io.h>
#else
#include <unistd.h>
#endif

#ifndef __linux__
#include <sys\stat.h>
#else
#include <cstring>
#include <sys/stat.h>
#endif

#include "extdll.h"
#include "util.h"
#include <enginecallback.h>

#include "bot.h"
#include "waypoint.h"

// linked list class.
#include "list.h"
List<char *> commanders;

#include <queue>
#include <utility>
#if defined(_MSC_VER) && _MSC_VER <= 1200
#include <map>
#else
#include <unordered_map>
#endif
#include "compat.h"

int last_area = -1;
bool area_on_last;

// Rocket jumping points
// The array to hold all the RJ waypoints, and the team they belong to.
int RJPoints[MAXRJWAYPOINTS][2];

extern int mod_id;
extern int m_spriteTexture;
extern bool botcamEnabled;

// my externs
extern bool blue_av[8];
extern bool red_av[8];
extern bool green_av[8];
extern bool yellow_av[8];

extern bool attack[4]; // teams attack
extern bool defend[4]; // teams defend

// tracks which waypoint types have been loaded for the current map, team by team
static int wp_type_exists[4];

// waypoints with information bits (flags)
WAYPOINT waypoints[MAX_WAYPOINTS];

// number of waypoints currently in use in the current map
int num_waypoints;

// waypoint author
char waypoint_author[256];

// declare the array of head pointers to the path structures...
// basically an array of linked lists
PATH *paths[MAX_WAYPOINTS];

// time that this waypoint was displayed (while editing)
float wp_display_time[MAX_WAYPOINTS];

static bool g_waypoint_paths = false; // have any paths been allocated?
bool g_waypoint_cache = false;
bool g_waypoint_on = false;
bool g_auto_waypoint = false;
bool g_path_waypoint = false;
bool g_find_waypoint = false;
long g_find_wp = 0;
bool g_path_connect = true;
bool g_path_oneway = false;
bool g_path_twoway = false;
static Vector last_waypoint;
static float f_path_time = 0.0;

static float a_display_time[MAX_WAYPOINTS];
bool g_area_def;

int wpt1;
int wpt2;

// area defs...
AREA areas[MAX_WAYPOINTS];
int num_areas;
static bool is_junction[MAX_WAYPOINTS];

static unsigned int route_num_waypoints;
unsigned int *shortest_path[4] = {nullptr, nullptr, nullptr, nullptr};
unsigned int *from_to[4] = {nullptr, nullptr, nullptr, nullptr};

static FILE *fp;

// FUNCTION PROTOTYPES
static void WaypointFloyds(unsigned int *shortest_path, unsigned int *from_to);
static void WaypointRouteInit();
static bool WaypointLoadVersion4(FILE *bfp, int number_of_waypoints);
static bool WaypointDeleteAimArtifact(const edict_t *pEntity);
static int WaypointAddInternal(const Vector &origin);
static void WaypointAddItemWaypoints();
static bool WaypointGenerateFromSpawn();

void WaypointDebug() {
   fp = UTIL_OpenFoxbotLog();
   if (fp != nullptr) {
      fprintf(fp, "WaypointDebug: LINKED LIST ERROR!!!\n");
      fclose(fp);
   }

   // int x = x - 1; // x is zero
   // int y = y / x; // cause an divide by zero exception

   return;
}

// free the linked list of waypoint path nodes...
void WaypointFree() {
   for (int i = 0; i < MAX_WAYPOINTS; i++) {
#ifdef _DEBUG
      int count = 0;
#endif

      if (paths[i]) {
         PATH *p = paths[i];

         while (p) // free the linked list
         {
            PATH *p_next = p->next; // save the link to next
            free(p);
            p = p_next;

#ifdef _DEBUG
            count++;
            if (count > 1000)
               WaypointDebug();
#endif
         }

         paths[i] = nullptr;
      }
   }
}

// initialize the waypoint structures...
void WaypointInit() {
   int i;

   // have any waypoint path nodes been allocated yet?
   if (g_waypoint_paths)
      WaypointFree(); // must free previously allocated path memory

   for (i = 0; i < 4; i++) {
      // if (shortest_path[i] != NULL)
      free(shortest_path[i]);

      // if (from_to[i] != NULL)
      free(from_to[i]);
   }

   // erase the name of the waypoint files author
   for (i = 0; i < 255; i++)
      waypoint_author[i] = '\0';

   for (i = 0; i < MAX_WAYPOINTS; i++) {
      waypoints[i].flags = 0;
      waypoints[i].script_flags = 0;
      waypoints[i].origin = Vector(0, 0, 0);

      wp_display_time[i] = 0.0;

      paths[i] = nullptr; // no paths allocated yet

      a_display_time[i] = 0.0;
      areas[i].flags = 0;
      areas[i].a = Vector(0, 0, 0);
      areas[i].b = Vector(0, 0, 0);
      areas[i].c = Vector(0, 0, 0);
      areas[i].d = Vector(0, 0, 0);
      areas[i].namea[0] = '\0';
      areas[i].nameb[0] = '\0';
      areas[i].namec[0] = '\0';
      areas[i].named[0] = '\0';

      is_junction[i] = false;
   }

   f_path_time = 0.0; // reset waypoint path display time

   num_waypoints = 0;
   num_areas = 0;

   last_waypoint = Vector(0, 0, 0);

   for (i = 0; i < 4; i++) {
      shortest_path[i] = nullptr;
      from_to[i] = nullptr;
   }
}

// helper for temporary waypoint creation
static int WaypointAddInternal(const Vector &origin) {
   if (num_waypoints >= MAX_WAYPOINTS)
      return -1;

   const int index = num_waypoints++;
   waypoints[index].flags = 0;
   waypoints[index].script_flags = 0;
   waypoints[index].origin = origin;
   wp_display_time[index] = gpGlobals->time;
   paths[index] = nullptr;
   return index;
}

// search map entities for pickups and add them as temporary waypoints
static void WaypointAddItemWaypoints() {
   int added = 0;
   for (int i = 1; i < gpGlobals->maxEntities && num_waypoints < MAX_WAYPOINTS; ++i) {
      edict_t *ent = INDEXENT(i);
      if (FNullEnt(ent))
         continue;

      const char *cls = STRING(ent->v.classname);
      if (cls == nullptr)
         continue;

      bool isHealth = strcmp(cls, "item_healthkit") == 0 || strcmp(cls, "item_health") == 0 || strcmp(cls, "item_battery") == 0;
      bool isAmmo = strncmp(cls, "ammo_", 5) == 0 || strncmp(cls, "tf_ammo", 7) == 0 || strcmp(cls, "tf_ammobox") == 0 || strcmp(cls, "item_shells") == 0 || strcmp(cls, "item_rockets") == 0 || strcmp(cls, "item_cells") == 0;

      if (!isHealth && !isAmmo)
         continue;

      if (ent->v.effects & EF_NODRAW)
         continue;

      if (WaypointFindNearest_V(ent->v.origin, 40.0f, -1) != -1)
         continue;

      const int idx = WaypointAddInternal(ent->v.origin);
      if (idx == -1)
         return;

      if (isHealth)
         waypoints[idx].flags |= W_FL_HEALTH;
      else if (isAmmo)
         waypoints[idx].flags |= W_FL_AMMO;

      const int near = WaypointFindNearest_V(ent->v.origin, REACHABLE_RANGE, -1);
      if (near != -1) {
         if (WaypointAddPath(idx, near))
            g_waypoint_paths = true;
         if (WaypointAddPath(near, idx))
            g_waypoint_paths = true;
      }

      added++;
   }

   if (added > 0)
      ALERT(at_console, "Added %d item goal waypoints.\n", added);
}

// generate waypoints using BFS from first spawn point
static bool WaypointGenerateFromSpawn() {
   edict_t *spawn = FIND_ENTITY_BY_CLASSNAME(nullptr, "info_player_deathmatch");
   if (FNullEnt(spawn))
      spawn = FIND_ENTITY_BY_CLASSNAME(nullptr, "info_player_start");
   if (FNullEnt(spawn))
      spawn = FIND_ENTITY_BY_CLASSNAME(nullptr, "info_player_teamspawn");
   if (FNullEnt(spawn)) {
      ALERT(at_console, "No spawn point found for temporary waypoints.\n");
      return false;
   }

   constexpr float grid = 128.0f;

   struct GridKey {
      int x, y, z;
      bool operator==(const GridKey &other) const {
         return x == other.x && y == other.y && z == other.z;
      }
   };

   struct GridKeyHash {
      size_t operator()(const GridKey &k) const noexcept {
         size_t h = static_cast<size_t>(k.x) * 73856093u;
         h ^= static_cast<size_t>(k.y) * 19349663u;
         h ^= static_cast<size_t>(k.z) * 83492791u;
         return h;
      }
   };

   std::queue<std::pair<Vector, int>> q;
   std::unordered_map<GridKey, int, GridKeyHash> visited;

   Vector start = spawn->v.origin;
   const GridKey startKey{static_cast<int>(floor(start.x / grid)),
                         static_cast<int>(floor(start.y / grid)),
                         static_cast<int>(floor(start.z / grid))};
   const int startIndex = WaypointAddInternal(start);
   if (startIndex == -1)
      return false;

   visited[startKey] = startIndex;
   q.push({start, startIndex});

   const Vector directions[6] = {Vector(grid, 0, 0),   Vector(-grid, 0, 0),
                                 Vector(0, grid, 0),   Vector(0, -grid, 0),
                                 Vector(0, 0, 50),     Vector(0, 0, -50)};

   while (!q.empty() && num_waypoints < 256) {
      std::pair<Vector, int> front = q.front();
      const Vector pos = front.first;
      const int idx = front.second;
      q.pop();
      for (int d = 0; d < 6; ++d) {
         const Vector next = pos + directions[d];
         const GridKey key{static_cast<int>(floor(next.x / grid)),
                           static_cast<int>(floor(next.y / grid)),
                           static_cast<int>(floor(next.z / grid))};
         if (visited.find(key) != visited.end())
            continue;

         if (!WaypointReachable(pos, next, spawn))
            continue;

         const int ni = WaypointAddInternal(next);
         if (ni == -1)
            return true;
         visited[key] = ni;
         if (WaypointAddPath(idx, ni))
            g_waypoint_paths = true;
         if (WaypointAddPath(ni, idx))
            g_waypoint_paths = true;
         q.push(std::make_pair(next, ni));
      }
   }

   WaypointAddItemWaypoints();

   ALERT(at_console, "Generated %d temporary waypoints.\n", num_waypoints);
   WaypointRouteInit();
   return num_waypoints > 0;
}

// add a path from one waypoint (add_index) to another (path_index)...
// Returns false on memory allocation failure.
int WaypointAddPath(const short int add_index, const short int path_index) {
   // don't do it if its greater than max distance
   if ((waypoints[add_index].origin - waypoints[path_index].origin).Length() > REACHABLE_RANGE)
      return true;

   PATH *p = paths[add_index];
   PATH *prev = nullptr;
   int i;

#ifdef _DEBUG
   int count = 0;
#endif

   // find an empty slot for new path_index...
   while (p != nullptr) {
      i = 0;

      while (i < MAX_PATH_INDEX) {
         // don't add the path if its already there?!
         if (p->index[i] == path_index)
            return true;

         if (p->index[i] == -1) {
            p->index[i] = path_index;

            return true;
         }

         i++;
      }

      prev = p;    // save the previous node in linked list
      p = p->next; // go to next node in linked list

#ifdef _DEBUG
      count++;
      if (count > 100)
         WaypointDebug();
#endif
   }

   // hmmm, should we only do this if prev->next !=NULL
   // nope loop only stops when complete...
   // ignore this unless future probs arise!

   p = static_cast<PATH *>(malloc(sizeof(PATH)));

   if (p == nullptr) {
      ALERT(at_error, "FoXBot - Error, memory allocation failed for waypoint path!");
      UTIL_BotLogPrintf("Memory allocation failed for waypoint path!\n");
      return false;
   }

   p->index[0] = path_index;
   p->index[1] = -1;
   p->index[2] = -1;
   p->index[3] = -1;
   p->next = nullptr;

   if (prev != nullptr)
      prev->next = p; // link new node into existing list

   if (paths[add_index] == nullptr)
      paths[add_index] = p; // save head point if necessary

   return true;
}

// delete all paths to this waypoint index...
void WaypointDeletePath(const short int del_index) {
   int i;

   // search all paths for this index...
   for (int index = 0; index < num_waypoints; index++) {
      PATH *p = paths[index];

#ifdef _DEBUG
      int count = 0;
#endif

      // search linked list for del_index...
      while (p != nullptr) {
         i = 0;

         while (i < MAX_PATH_INDEX) {
            if (p->index[i] == del_index) {
               p->index[i] = -1; // unassign this path
            }

            i++;
         }

         p = p->next; // go to next node in linked list

#ifdef _DEBUG
         count++;
         if (count > 100)
            WaypointDebug();
#endif
      }
   }
}

// delete a path from a waypoint (path_index) to another waypoint
// (del_index)...
void WaypointDeletePath(const short int path_index, const short int del_index) {
   PATH *p = paths[path_index];
   int i;

#ifdef _DEBUG
   int count = 0;
#endif

   // search linked list for del_index...
   while (p != nullptr) {
      i = 0;

      while (i < MAX_PATH_INDEX) {
         if (p->index[i] == del_index) {
            p->index[i] = -1; // unassign this path
         }

         i++;
      }

      p = p->next; // go to next node in linked list

#ifdef _DEBUG
      count++;
      if (count > 100)
         WaypointDebug();
#endif
   }
}

// find a path from the current waypoint. (pPath MUST be NULL on the
// initial call. subsequent calls will return other paths if they exist.)
int WaypointFindPath(PATH **pPath, int *path_index, const int waypoint_index, const int team) {
   int index;

   if (*pPath == nullptr) {
      *pPath = paths[waypoint_index];
      *path_index = 0;
   }

   if (*path_index == MAX_PATH_INDEX) {
      *path_index = 0;

      *pPath = (*pPath)->next; // go to next node in linked list
   }

#ifdef _DEBUG
   int count = 0;
#endif

   while (*pPath != nullptr) {
      while (*path_index < MAX_PATH_INDEX) {
         if ((*pPath)->index[*path_index] != -1) // found a path?
         {
            // save the return value
            index = (*pPath)->index[*path_index];

            // skip this path if next waypoint is team specific and
            // NOT this team
            if (team != -1 && waypoints[index].flags & W_FL_TEAM_SPECIFIC && (waypoints[index].flags & W_FL_TEAM) != team) {
               (*path_index)++;
               continue;
            }

            // set up stuff for subsequent calls...
            (*path_index)++;

            return index;
         }

         (*path_index)++;
      }

      *path_index = 0;

      *pPath = (*pPath)->next; // go to next node in linked list

#ifdef _DEBUG
      count++;
      if (count > 100)
         WaypointDebug();
#endif
   }

   return -1;
}

// find the index of the nearest waypoint to the indicated player
// (-1 if not found)
// This function also checks that the waypoint is visible to the specified entity.
int WaypointFindNearest_E(const edict_t *pEntity, const float range, const int team) {
   int min_index = -1;
   double min_distance_squared = range * range + 0.1f;
   TraceResult tr;

   // bit field of waypoint types to ignore
   static constexpr WPT_INT32 ignoreFlags = 0 + (W_FL_DELETED | W_FL_AIMING);

   // find the nearest waypoint...
   for (int i = 0; i < num_waypoints; i++) {
      // skip waypoints we don't want to consider
      if (waypoints[i].flags & ignoreFlags)
         continue;

      // skip this waypoint if it's team specific and teams don't match
      if (team != -1 && waypoints[i].flags & W_FL_TEAM_SPECIFIC && (waypoints[i].flags & W_FL_TEAM) != team)
         continue;

      const Vector distance = waypoints[i].origin - pEntity->v.origin;
      const double distance_squared = distance.x * distance.x + distance.y * distance.y + distance.z * distance.z;

      if (distance_squared < min_distance_squared) {
         // if waypoint is visible from current position
         // (even behind head)...
         UTIL_TraceLine(pEntity->v.origin + pEntity->v.view_ofs, waypoints[i].origin, ignore_monsters, pEntity->v.pContainingEntity, &tr);

         if (tr.flFraction >= 1.0) {
            min_index = i;
            min_distance_squared = distance_squared;
         }
      }
   }

   return min_index;
}

// pick an origin, and find the nearest waypoint to it
// This wont check for visibility.
int WaypointFindNearest_V(const Vector &v_src, const float range, const int team) {
   if (num_waypoints < 1)
      return -1;

   int min_index = -1;
   float min_distance = range;

   // bit field of waypoint types to ignore
   static constexpr WPT_INT32 ignoreFlags = 0 + (W_FL_DELETED | W_FL_AIMING);

   // find the nearest waypoint...
   for (int index = 0; index < num_waypoints; index++) {
      // skip waypoints we don't want to consider
      if (waypoints[index].flags & ignoreFlags)
         continue;

      // skip this waypoint if it's team specific and teams don't match
      if (team != -1 && waypoints[index].flags & W_FL_TEAM_SPECIFIC && (waypoints[index].flags & W_FL_TEAM) != team)
         continue;

      const float distance = (waypoints[index].origin - v_src).Length();

      if (distance < min_distance) {
         min_index = index;
         min_distance = distance;
      }
   }
   return min_index;
}

// Find the nearest waypoint to the source postition and return the index of
// that waypoint.
// If pEntity is not NULL then the waypoint found must be visible to that entity.
// Also, you can specify waypoint flags that you wish to exclude from the search.
int WaypointFindNearest_S(const Vector &v_src, edict_t *pEntity, const float range, const int team, const WPT_INT32 ignore_flags) {
   int min_index = -1;
   double min_distance_squared = range * range + 0.1f;
   TraceResult tr;

   // find the nearest waypoint...
   for (int index = 0; index < num_waypoints; index++) {
      // skip deleted waypoints
      if (waypoints[index].flags & W_FL_DELETED || waypoints[index].flags & W_FL_AIMING)
         continue;

      // skip waypoints we should ignore
      if (waypoints[index].flags & ignore_flags)
         continue;

      // skip this waypoint if it's team specific and teams don't match
      if (team != -1 && waypoints[index].flags & W_FL_TEAM_SPECIFIC && (waypoints[index].flags & W_FL_TEAM) != team)
         continue;

      // square the Manhattan distance
      const Vector distance = waypoints[index].origin - v_src;
      const double distance_squared = distance.x * distance.x + distance.y * distance.y + distance.z * distance.z;

      // if it's the nearest found so far
      if (distance_squared < min_distance_squared) {
         // check if the waypoint is visible from
         // the source position or a specified entity
         if (pEntity != nullptr)
            UTIL_TraceLine(v_src, waypoints[index].origin, dont_ignore_monsters, pEntity->v.pContainingEntity, &tr);
         else
            UTIL_TraceLine(waypoints[index].origin, v_src, ignore_monsters, nullptr, &tr);

         // it is visible, so store it
         if (tr.flFraction >= 1.0) {
            min_index = index;
            min_distance_squared = distance_squared;
         }
      }
   }

   return min_index;
}

// This function is like WaypointFindNearest() but you can specify the
// ideal kind of distance you want for the waypoint returned.
// If no waypoint of suitable distance was found it may return a nearer
// waypoint it found instead.
// If chooseRandom is true then the search starts from a random waypoint.
// Returns -1 if no waypoint was found that is in visible range.
int WaypointFindInRange(const Vector &v_src, const float min_range, const float max_range, const int team, const bool chooseRandom) {
   TraceResult tr;
   int nextBestWP = -1; // just in case one wasn't found in ideal range

   int i = 0;

   // pick a random waypoint to start searching from if requested to
   if (chooseRandom)
      i = RANDOM_LONG(0, num_waypoints - 1);

   // bit field of waypoint types to ignore
   static constexpr WPT_INT32 ignoreFlags = 0 + (W_FL_DELETED | W_FL_AIMING);

   // start the search
   for (int waypoints_checked = 0; waypoints_checked < num_waypoints; waypoints_checked++, i++) {
      // wrap the search if it exceeds the number of available waypoints
      if (i >= num_waypoints)
         i = 0;

      // skip waypoints we don't want to consider
      if (waypoints[i].flags & ignoreFlags)
         continue;

      // skip this waypoint if it's team specific and teams don't match
      if (team != -1 && waypoints[i].flags & W_FL_TEAM_SPECIFIC && (waypoints[i].flags & W_FL_TEAM) != team)
         continue;

      const float distance = (waypoints[i].origin - v_src).Length();

      // if this waypoint is within range
      if (distance < max_range) {
         UTIL_TraceLine(waypoints[i].origin, v_src, ignore_monsters, nullptr, &tr);

         // if the source is visible from this waypoint
         if (tr.flFraction >= 1.0) {
            // a cool laser effect (for debugging purposes)
            // 	WaypointDrawBeam(INDEXENT(1), waypoints[i].origin,
            // 		v_src, 10, 2, 50, 250, 50, 200, 10);

            // it's within the ideal range, purr-fect
            if (distance > min_range)
               return i;

            // remember this waypoint as a backup
            nextBestWP = i;
         }
      }
   }

   // no waypoint was found in ideal range, use the backup instead
   if (nextBestWP != -1)
      return nextBestWP;

   return -1; // none found
}

// find the goal nearest to the source waypoint matching the "flags" bits
// and within range and return the index of that waypoint...
int WaypointFindNearestGoal(const int srcWP, const int team, int range, const WPT_INT32 flags) {
   // sanity check
   if (num_waypoints < 1 || srcWP < 0 || srcWP >= num_waypoints) {
      //	UTIL_BotLogPrintf("bad srcWP\n");
      return -1;
   }

   int min_index = -1;

   // find the nearest waypoint with the matching flags...
   for (int index = 0; index < num_waypoints; index++) {
      if (flags == 0) // was a plain waypoint with no flags requested?
      {
         if (waypoints[index].flags != 0)
            continue;
      } else if (!(waypoints[index].flags & flags))
         continue; // skip this waypoint if the flags don't match

      // skip any deleted waypoints or aiming waypoints
      if (waypoints[index].flags & W_FL_DELETED || waypoints[index].flags & W_FL_AIMING)
         continue;

      // Skip unavailable waypoints.
      if (!WaypointAvailable(index, team))
         continue;

      if (index == srcWP)
         continue; // skip the source waypoint

      const int distance = WaypointDistanceFromTo(srcWP, index, team);

      if (distance < range && distance > 0) {
         min_index = index;
         range = distance;
      }
   }

   return min_index;
}

#if 0 // this function is currently unused
// find the goal nearest to the source position (v_src) matching the
// "flags" bits and return the index of that waypoint...
// Returns -1 if one wasn't found
int WaypointFindNearestGoal_R(Vector v_src, edict_t* pEntity,
	const float range, const int team, const WPT_INT32 flags)
{
	// sanity check
	if (num_waypoints < 1)
		return -1;

	int index;
	int min_index = -1;
	float distance;
	float min_distance = range;

	// find the nearest waypoint with the matching flags...
	for (index = 0; index < num_waypoints; index++)
	{
		if (flags == 0)  // was a plain waypoint with no flags requested?
		{
			if (waypoints[index].flags != 0)
				continue;
		}
		else if (!(waypoints[index].flags & flags))
			continue;  // skip this waypoint if the flags don't match

		// skip any deleted waypoints or aiming waypoints
		if (waypoints[index].flags & W_FL_DELETED
			|| waypoints[index].flags & W_FL_AIMING)
			continue;

		// Skip unavailable waypoints.
		if (!WaypointAvailable(index, team))
			continue;

		distance = (waypoints[index].origin - v_src).Length();

		if (distance < min_distance)
		{
			min_index = index;
			min_distance = distance;
		}
	}

	return min_index;
}
#endif

// find a random goal matching the "flags" bits and return the index of
// that waypoint...
// If source_WP is not -1 then this will make sure a path exists to the
// waypoint it returns.
int WaypointFindRandomGoal(const int source_WP, const int team, const WPT_INT32 flags) {
   if (num_waypoints < 1 || source_WP < 0 || source_WP >= num_waypoints)
      return -1;

   static int indexes[50]; // made static for speed reasons
   int count = 0;

   // start from a random waypoint in case there are more matching waypoints
   // than this function is ready to keep a list of
   int index = RANDOM_LONG(0, num_waypoints - 1);

   // find all the waypoints with the matching flags...
   for (int i = 0; i < num_waypoints; i++, index++) {
      // wrap the search if it exceeds the number of available waypoints
      if (index >= num_waypoints)
         index = 0;

      if (flags == 0) // was a plain waypoint with no flags requested?
      {
         if (waypoints[index].flags != 0)
            continue;
      } else if (!(waypoints[index].flags & flags))
         continue; // skip this waypoint if the flags don't match

      // skip any deleted or aiming waypoints
      if (waypoints[index].flags & W_FL_DELETED || waypoints[index].flags & W_FL_AIMING)
         continue;

      // need to skip wpt if not available to the bots team
      // also - make sure a route exists to this waypoint
      if (!WaypointAvailable(index, team) || WaypointRouteFromTo(source_WP, index, team) == -1)
         continue;

      // found a suitable waypoint
      indexes[count] = index;
      ++count;

      if (count >= 50)
         break; // we have filled the list
   }

   if (count == 0) // no matching waypoints found
      return -1;

   index = RANDOM_LONG(1, count) - 1;

   return indexes[index];
}

// find a random goal within range matching the "flags" bits and return
// the index of that waypoint...
// This function will make sure a path exists to the waypoint it returns.
int WaypointFindRandomGoal_D(const int source_WP, const int team, const int range, const WPT_INT32 flags) {
   if (num_waypoints < 1 || source_WP < 0 || source_WP >= num_waypoints)
      return -1;

   static int indexes[50]; // made static for speed reasons
   int count = 0;

   // start from a random waypoint in case there are more matching waypoints
   // than this function is ready to keep a list of
   int index = RANDOM_LONG(0, num_waypoints - 1);

   // find all the waypoints with the matching flags...
   for (int i = 0; i < num_waypoints; i++, index++) {
      // wrap the search if it exceeds the number of available waypoints
      if (index >= num_waypoints)
         index = 0;

      if (flags == 0) // was a plain waypoint with no flags requested?
      {
         if (waypoints[index].flags != 0)
            continue;
      } else if (!(waypoints[index].flags & flags))
         continue; // skip this waypoint if the flags don't match

      // skip any deleted or aiming waypoints
      if (waypoints[index].flags & W_FL_DELETED || waypoints[index].flags & W_FL_AIMING)
         continue;

      if (index == source_WP)
         continue; // we're looking for a new waypoint, duh!

      const int routeDistance = WaypointDistanceFromTo(source_WP, index, team);

      // need to skip wpt if not available to the bots team
      // or it's too far away
      if (routeDistance == -1 || routeDistance > range || !WaypointAvailable(index, team))
         continue;

      // found a suitable waypoint
      indexes[count] = index;
      ++count;

      if (count >= 50)
         break; // we have filled the list
   }

   if (count == 0) // no matching waypoints found
      return -1;

   index = RANDOM_LONG(1, count) - 1;

   return indexes[index];
}

// find a random goal within a range of a position (v_src) matching the
// "flags" bits and return the index of that waypoint...
int WaypointFindRandomGoal_R(const Vector &v_src, const bool checkVisibility, const float range, const int team, const WPT_INT32 flags) {
   if (num_waypoints < 1)
      return -1;

   static int indexes[50]; // made static for speed reasons
   int count = 0;

   // start from a random waypoint in case there are more matching waypoints
   // than this function is ready to remember and choose from
   int index = RANDOM_LONG(0, num_waypoints - 1);

   TraceResult tr;

   // find all the waypoints with the matching flags...
   for (int i = 0; i < num_waypoints; i++, index++) {
      // wrap the search if it exceeds the number of available waypoints
      if (index >= num_waypoints)
         index = 0;

      if (flags == 0) // was a plain waypoint with no flags requested?
      {
         if (waypoints[index].flags != 0)
            continue;
      } else if (!(waypoints[index].flags & flags))
         continue; // skip this waypoint if the flags don't match

      // skip any deleted or aiming waypoints
      if (waypoints[index].flags & W_FL_DELETED || waypoints[index].flags & W_FL_AIMING)
         continue;

      // need to skip wpt if not available to bots team
      if (!WaypointAvailable(index, team))
         continue;

      if (VectorsNearerThan(waypoints[index].origin, v_src, range)) {
         if (checkVisibility)
            UTIL_TraceLine(v_src, waypoints[index].origin, ignore_monsters, nullptr, &tr);

         if (!checkVisibility || tr.flFraction >= 1.0) {
            indexes[count] = index;
            ++count;

            if (count >= 50)
               break; // we have filled the list
         }
      }
   }

   if (count == 0) // no matching waypoints found
      return -1;

   index = RANDOM_LONG(1, count) - 1;

   return indexes[index];
}

// This function attempts to find a randomly selected detpack waypoint
// that is suitable for setting a detpack on.
// It will return -1 on failure to find such a waypoint.
int WaypointFindDetpackGoal(const int srcWP, const int team) {
   if (num_waypoints < 1 || srcWP == -1)
      return -1;

   int WP_list[10];
   int total = 0;

   // find all the waypoints with the matching flags...
   for (int index = 0; index < num_waypoints; index++) {
      if (!(waypoints[index].flags & W_FL_TFC_DETPACK_CLEAR) && !(waypoints[index].flags & W_FL_TFC_DETPACK_SEAL))
         continue; // skip this waypoint if the flags don't match

      // skip any deleted or aiming waypoints
      if (waypoints[index].flags & W_FL_DELETED || waypoints[index].flags & W_FL_AIMING)
         continue;

      // skip it if not available to the bots team
      // also - make sure a route exists to this waypoint
      if (!WaypointAvailable(index, team) || WaypointRouteFromTo(srcWP, index, team) == -1)
         continue;

      WP_list[total] = index;
      ++total;

      if (total > 8)
         break;
   }

   // pick waypoints out of the list randomly until we find a suitable one
   if (total > 0) {
      while (total > 0) {
         // pick a waypoint from the list at random
         const int rand_index = RANDOM_LONG(0, total - 1);

         // check if it needs clearing
         if (waypoints[WP_list[rand_index]].flags & W_FL_TFC_DETPACK_CLEAR && DetpackClearIsBlocked(WP_list[rand_index]))
            return WP_list[rand_index];

         // check if it needs sealing
         if (waypoints[WP_list[rand_index]].flags & W_FL_TFC_DETPACK_SEAL && DetpackSealIsClear(WP_list[rand_index]))
            return WP_list[rand_index];

         --total; // shrink the list

         // this detpack waypoint is unsuitable
         // replace it with the last waypoint in the list
         WP_list[rand_index] = WP_list[total];
      }
   }

   return -1;
}

// This function returns true if a specified W_FL_TFC_DETPACK_CLEAR waypoint
// needs detpacking to blast it open.
bool DetpackClearIsBlocked(const int index) {
   TraceResult tr;

   // start checking
   PATH *p = paths[index];
   int path_total = 0;
   while (p != nullptr) {
      for (int i = 0; i < MAX_PATH_INDEX; i++) {
         // test for an obstruction
         if (p->index[i] != -1) {
            ++path_total;
            UTIL_TraceLine(waypoints[index].origin, waypoints[p->index[i]].origin, ignore_monsters, nullptr, &tr);

            if (tr.flFraction < 1.0)
               return true; // a path is blocked by something
         }
      }

      p = p->next; // go to next node in linked list
   }

   // return true only if no blockages were found
   // and there was less than two paths from the waypoint
   // this test is done to maintain backwards compatibility with old maps
   // from before Foxbot 0.76 that only used one type of detpack waypoint tag
   if (path_total < 2)
      return true;

   return false; // waypoint is unsuitable for detpacking
}

// This function returns true if a specified W_FL_TFC_DETPACK_SEAL waypoint
// needs detpacking to blast it closed.
bool DetpackSealIsClear(const int index) {
   TraceResult tr;

   // start checking
   PATH *p = paths[index];
   while (p != nullptr) {
      for (int i = 0; i < MAX_PATH_INDEX; i++) {
         // test for an obstruction
         if (p->index[i] != -1) {
            UTIL_TraceLine(waypoints[index].origin, waypoints[p->index[i]].origin, ignore_monsters, nullptr, &tr);

            if (tr.flFraction < 1.0)
               return false; // a path is blocked by something
         }
      }

      p = p->next; // go to next node in linked list
   }

   // all paths are clear - waypoint is suitable for detpacking
   return true;
}

// find the nearest "special" aiming waypoint (for sniper aiming)...
// Returns -1 if one wasn't found
int WaypointFindNearestAiming(const Vector &r_v_origin) {
   if (num_waypoints < 1)
      return -1;

   int min_index = -1;
   float min_distance = 200.0f;

   // search for nearby aiming waypoint...
   for (int index = 0; index < num_waypoints; index++) {
      if ((waypoints[index].flags & W_FL_AIMING) == 0)
         continue; // skip any NON aiming waypoints

      if (waypoints[index].flags & W_FL_DELETED)
         continue; // skip any deleted waypoints

      // ignore distant waypoints(most aim waypoints will be too far away)
      if (!VectorsNearerThan(r_v_origin, waypoints[index].origin, 200.0f))
         continue;

      // find the nearest of the near
      const float distance = (r_v_origin - waypoints[index].origin).Length();
      if (distance < min_distance) {
         min_index = index;
         min_distance = distance;
      }
   }

   return min_index;
}

void WaypointDrawBeam(edict_t *pEntity, const Vector &start, const Vector &end, int width, int noise, int red, int green, int blue, int brightness, int speed) {
   MESSAGE_BEGIN(MSG_ONE, SVC_TEMPENTITY, nullptr, pEntity);
   WRITE_BYTE(TE_BEAMPOINTS);
   WRITE_COORD(start.x);
   WRITE_COORD(start.y);
   WRITE_COORD(start.z);
   WRITE_COORD(end.x);
   WRITE_COORD(end.y);
   WRITE_COORD(end.z);
   WRITE_SHORT(m_spriteTexture);
   WRITE_BYTE(1);     // framestart
   WRITE_BYTE(10);    // framerate
   WRITE_BYTE(10);    // life in 0.1's
   WRITE_BYTE(width); // width
   WRITE_BYTE(noise); // noise

   WRITE_BYTE(red);   // r, g, b
   WRITE_BYTE(green); // r, g, b
   WRITE_BYTE(blue);  // r, g, b

   WRITE_BYTE(brightness); // brightness
   WRITE_BYTE(speed);      // speed
   MESSAGE_END();
}

void WaypointAdd(edict_t *pEntity) {
   if (num_waypoints >= MAX_WAYPOINTS)
      return;

   edict_t *pent = nullptr;
   constexpr float radius = 40.0f;
   int index = 0;

   // find the next available slot for the new waypoint...
   while (index < num_waypoints) {
      if (waypoints[index].flags & W_FL_DELETED)
         break;

      index++;
   }

   waypoints[index].flags = 0;
   waypoints[index].script_flags = 0;

   // store the origin (location) of this waypoint (use entity origin)
   waypoints[index].origin = pEntity->v.origin;

   // store the last used waypoint for the auto waypoint code...
   last_waypoint = pEntity->v.origin;

   // set the time that this waypoint was originally displayed...
   wp_display_time[index] = gpGlobals->time;

   Vector start = pEntity->v.origin - Vector(0, 0, 34);
   Vector end = start + Vector(0, 0, 68);

   if (pEntity->v.flags & FL_DUCKING) {
      waypoints[index].flags |= W_FL_CROUCH; // crouching waypoint

      start = pEntity->v.origin - Vector(0, 0, 17);
      end = start + Vector(0, 0, 34);
   }

   if (pEntity->v.movetype == MOVETYPE_FLY)
      waypoints[index].flags |= W_FL_LADDER; // waypoint on a ladder

   //********************************************************
   // look for lift, ammo, flag, health, armor, etc.
   //********************************************************

   while ((pent = FIND_ENTITY_IN_SPHERE(pent, pEntity->v.origin, radius)) != nullptr && !FNullEnt(pent)) {
      char item_name[64];
      strcpy(item_name, STRING(pent->v.classname));

      if (strcmp("item_healthkit", item_name) == 0) {
         ClientPrint(pEntity, HUD_PRINTCONSOLE, "found a healthkit!\n");
         waypoints[index].flags |= W_FL_HEALTH;
      }

      if (strncmp("item_armor", item_name, 10) == 0) {
         ClientPrint(pEntity, HUD_PRINTCONSOLE, "found some armor!\n");
         waypoints[index].flags |= W_FL_ARMOR;
      }

      // *************
      // LOOK FOR AMMO
      // *************
   }

   // draw a blue waypoint
   WaypointDrawBeam(pEntity, start, end, 30, 0, 0, 0, 255, 250, 5);

   EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/xbow_hit1.wav", 1.0, ATTN_NORM, 0, 100);

   // increment total number of waypoints if adding at end of array...
   if (index == num_waypoints)
      num_waypoints++;

   // delete stuff just in case.
   WaypointDeletePath(index);
   // delete from index to other..cus his delete function dont!

   PATH *p = paths[index];
   int i;

#ifdef _DEBUG
   int count = 0;
#endif

   // search linked list for del_index...
   while (p != nullptr) {
      i = 0;
      while (i < MAX_PATH_INDEX) {
         p->index[i] = -1; // unassign this path
         i++;
      }
      p = p->next; // go to next node in linked list

#ifdef _DEBUG
      count++;
      if (count > 100)
         WaypointDebug();
#endif
   }

   if (g_path_connect) {
      // calculate all the paths to this new waypoint
      for (i = 0; i < num_waypoints; i++) {
         if (i == index)
            continue; // skip the waypoint that was just added

         if (waypoints[i].flags & W_FL_AIMING || waypoints[i].flags & W_FL_DELETED)
            continue; // skip deleted/aiming wpts!!

         // check if the waypoint is reachable from the new one (one-way)
         if (WaypointReachable(pEntity->v.origin, waypoints[i].origin, pEntity)) {
            WaypointAddPath(index, i);
         }

         // check if the new one is reachable from the waypoint (other way)
         if (WaypointReachable(waypoints[i].origin, pEntity->v.origin, pEntity)) {
            WaypointAddPath(i, index);
         }
      }
   }
}

void WaypointAddAiming(edict_t *pEntity) {
   if (num_waypoints >= MAX_WAYPOINTS)
      return;

   // edict_t *pent = NULL;
   int index = 0;

   // find the next available slot for the new waypoint...
   while (index < num_waypoints) {
      if (waypoints[index].flags & W_FL_DELETED)
         break;

      index++;
   }

   waypoints[index].flags = W_FL_AIMING; // aiming waypoint

   Vector v_angle = pEntity->v.v_angle;

   v_angle.x = 0; // reset pitch to horizontal
   v_angle.z = 0; // reset roll to level

   UTIL_MakeVectors(v_angle);

   // store the origin (location) of this waypoint (use entity origin)
   waypoints[index].origin = pEntity->v.origin + gpGlobals->v_forward * 25;

   // set the time that this waypoint was originally displayed...
   wp_display_time[index] = gpGlobals->time;

   const Vector start = pEntity->v.origin - Vector(0, 0, 10);
   const Vector end = start + Vector(0, 0, 14);

   // draw a blue waypoint
   WaypointDrawBeam(pEntity, start, end, 30, 0, 0, 0, 255, 250, 5);

   EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/xbow_hit1.wav", 1.0, ATTN_NORM, 0, 100);

   // increment total number of waypoints if adding at end of array...
   if (index == num_waypoints)
      num_waypoints++;
}

// This function is run when you type 'waypoint delete' in the console.
void WaypointDelete(edict_t *pEntity) {
   if (num_waypoints < 1)
      return;

   if (WaypointDeleteAimArtifact(pEntity))
      return;

   const int index = WaypointFindNearest_E(pEntity, 50.0, -1);

   if (index == -1)
      return;

   // update by yuraj - check if this waypoint can be aimed
   if (waypoints[index].flags & W_FL_SNIPER || waypoints[index].flags & W_FL_TFC_SENTRY || waypoints[index].flags & W_FL_TFC_TELEPORTER_ENTRANCE || waypoints[index].flags & W_FL_TFC_TELEPORTER_EXIT ||
       waypoints[index].flags & W_FL_TFC_PL_DEFEND || waypoints[index].flags & W_FL_TFC_PIPETRAP) {
      int min_index = -1;
      float min_distance = 9999.0f;

      // search for nearby aiming waypoint and delete it also...
      for (int i = 0; i < num_waypoints; i++) {
         if (waypoints[i].flags & W_FL_DELETED)
            continue; // skip any deleted waypoints

         if ((waypoints[i].flags & W_FL_AIMING) == 0)
            continue;

         const float distance = (waypoints[i].origin - waypoints[index].origin).Length();

         if (distance < min_distance && distance < 40.0f) {
            min_index = i;
            min_distance = distance;
         }
      }

      if (min_index != -1) {
         waypoints[min_index].flags = W_FL_DELETED; // not being used
         waypoints[min_index].script_flags = 0;

         waypoints[min_index].origin = Vector(0, 0, 0);

         wp_display_time[min_index] = 0.0;
      }
   }

   // delete any paths that lead to this index...
   WaypointDeletePath(index);

#ifdef _DEBUG
   int count = 0;
#endif

   // free the path for this index...

   if (paths[index] != nullptr) {
      PATH *p = paths[index];

      while (p) // free the linked list
      {
         PATH *p_next = p->next; // save the link to next
         free(p);
         p = p_next;

#ifdef _DEBUG
         count++;
         if (count > 100)
            WaypointDebug();
#endif
      }

      paths[index] = nullptr;
   }

   // delete the waypoint
   waypoints[index].flags = W_FL_DELETED; // not being used
   waypoints[index].script_flags = 0;
   waypoints[index].origin = Vector(0, 0, 0);

   wp_display_time[index] = 0.0;

   EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/mine_activate.wav", 1.0, ATTN_NORM, 0, 100);
}

// This function checks for the nearest aim waypoint and and will delete it
// if it is an artifact(i.e. is not attached to another waypoint).
// It returns false if it did not delete an aim artifact.
static bool WaypointDeleteAimArtifact(const edict_t *pEntity) {
   if (num_waypoints < 1)
      return false;

   int min_index = -1;
   float min_distance = 400.0f;

   // find the nearest aim waypoint...
   for (int i = 0; i < num_waypoints; i++) {
      // skip all non-aiming waypoints
      if (!(waypoints[i].flags & W_FL_AIMING))
         continue;

      if (waypoints[i].flags & W_FL_DELETED)
         continue; // skip any deleted waypoints

      const float distance = (waypoints[i].origin - pEntity->v.origin).Length();

      if (distance < min_distance) {
         min_index = i;
         min_distance = distance;
      }
   }

   // abort if the nearest waypoint is not an aim waypoint
   if (min_index == -1)
      return false;

   TraceResult tr;
   bool waypoint_is_artifact = true;

   // found an aim waypoint, now to find out if it is an artifact
   for (int j = 0; j < num_waypoints; j++) {
      // skip any deleted or aiming waypoints
      if (waypoints[j].flags & W_FL_DELETED || waypoints[j].flags & W_FL_AIMING)
         continue;

      const float distance = (waypoints[j].origin - waypoints[min_index].origin).Length();

      // if this waypoint is near enough to the aim waypoint
      if (distance < 125.0f) {
         UTIL_TraceLine(waypoints[min_index].origin, waypoints[j].origin, ignore_monsters, nullptr, &tr);

         // if there is line of sight from this waypoint to the
         // aim waypoint
         if (tr.flFraction >= 1.0) {
            waypoint_is_artifact = false;

            // draw a beam to the nearest waypoint found to show
            // why this aim waypoint will not be deleted
            WaypointDrawBeam(INDEXENT(1), waypoints[min_index].origin, waypoints[j].origin, 10, 2, 250, 50, 50, 200, 10);

            break;
         }
      }
   }

   // delete this aim waypoint if it's an artifact
   if (waypoint_is_artifact) {
      waypoints[min_index].flags = W_FL_DELETED;
      waypoints[min_index].script_flags = 0;
      waypoints[min_index].origin = Vector(0, 0, 0);

      wp_display_time[min_index] = 0.0;
      return true;
   }

   return false;
}

// allow player to manually create a path from one waypoint to another
void WaypointCreatePath(edict_t *pEntity, const int cmd) {
   static int waypoint1 = -1; // initialized to unassigned
   static int waypoint2 = -1; // initialized to unassigned

   if (cmd == 1) // assign source of path
   {
      waypoint1 = WaypointFindNearest_E(pEntity, 50.0, -1);

      if (waypoint1 == -1) {
         // play "cancelled" sound...
         EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_moveselect.wav", 1.0, ATTN_NORM, 0, 100);
         return;
      }

      // play "start" sound...
      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_hudoff.wav", 1.0, ATTN_NORM, 0, 100);

      return;
   }

   if (cmd == 2) // assign dest of path and make path
   {
      waypoint2 = WaypointFindNearest_E(pEntity, 50.0, -1);

      if (waypoint1 == -1 || waypoint2 == -1) {
         // play "error" sound...
         EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_denyselect.wav", 1.0, ATTN_NORM, 0, 100);

         return;
      }

      WaypointAddPath(waypoint1, waypoint2);

      // play "done" sound...
      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_hudon.wav", 1.0, ATTN_NORM, 0, 100);
   }
}

// allow player to manually remove a path from one waypoint to another
void WaypointRemovePath(edict_t *pEntity, const int cmd) {
   static int waypoint1 = -1; // initialized to unassigned
   static int waypoint2 = -1; // initialized to unassigned

   if (cmd == 1) // assign source of path
   {
      waypoint1 = WaypointFindNearest_E(pEntity, 50.0, -1);

      if (waypoint1 == -1) {
         // play "cancelled" sound...
         EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_moveselect.wav", 1.0, ATTN_NORM, 0, 100);

         return;
      }

      // play "start" sound...
      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_hudoff.wav", 1.0, ATTN_NORM, 0, 100);

      return;
   }

   if (cmd == 2) // assign dest of path and make path
   {
      waypoint2 = WaypointFindNearest_E(pEntity, 50.0, -1);

      if (waypoint1 == -1 || waypoint2 == -1) {
         // play "error" sound...
         EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_denyselect.wav", 1.0, ATTN_NORM, 0, 100);

         return;
      }

      WaypointDeletePath(waypoint1, waypoint2);

      // play "done" sound...
      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_hudon.wav", 1.0, ATTN_NORM, 0, 100);
   }
}

// This function can tell you if the the waypoint type you specify has
// been loaded from the current map file for the specific team.
// e.g. It can tell you if team 0 have Sniper waypoints.
// Set team to -1 if you are interested in non-team specific waypoints.
bool WaypointTypeExists(const WPT_INT32 flags, const int team) {
   if (team == -1    // non-team specific
       || team == 0) // team 0 - blue team
   {
      if (wp_type_exists[0] & flags)
         return true;
   } else if (team == 1) {
      if (wp_type_exists[1] & flags)
         return true;
   } else if (team == 2) {
      if (wp_type_exists[2] & flags)
         return true;
   } else if (team == 3) {
      if (wp_type_exists[3] & flags)
         return true;
   }

   return false;
}

bool WaypointLoad(edict_t *pEntity) {
   char mapname[64];
   char filename[256];
   WAYPOINT_HDR header;
   char msg[256];
   int index, i;
   short int num;
   short int path_index;

   // reset our knowledge of what waypoint types have been loaded
   wp_type_exists[0] = 0;
   wp_type_exists[1] = 0;
   wp_type_exists[2] = 0;
   wp_type_exists[3] = 0;

   ProcessCommanderList();

   strcpy(mapname, STRING(gpGlobals->mapname));
   strcat(mapname, ".fwp");

   // look for the waypoint file in the /waypoints directory
   UTIL_BuildFileName(filename, 255, "waypoints", mapname);
   FILE *bfp = fopen(filename, "rb");

   // if file exists, read the waypoint data from it
   if (bfp != nullptr) {
      if (IS_DEDICATED_SERVER())
         printf("loading waypoint file: %s\n", filename);

      // read in the waypoint header
      fread(&header, sizeof header, 1, bfp);

      header.filetype[7] = 0; // null terminate the filetype string

      // make sure the waypoint file is Foxbot specific
      if (strcmp(header.filetype, "FoXBot") != 0) {
         if (pEntity) {
            sprintf(msg, "%s isn't a valid FoXBot waypoint file\n", filename);
            ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
         }

         if (IS_DEDICATED_SERVER())
            printf("%s isn't a valid FoXBot waypoint file\n", filename);

         fclose(bfp);
         return false;
      }
      // make sure the waypoint file was meant for the current map
      header.mapname[31] = 0;

      if (strcasecmp(header.mapname, STRING(gpGlobals->mapname)) != 0) {
         if (pEntity) {
            sprintf(msg, "%s FoXBot waypoints aren't for this map\n", filename);
            ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
         }

         if (IS_DEDICATED_SERVER())
            printf("%s FoXBot waypoints aren't for this map\n", filename);

         fclose(bfp);
         return false;
      }

      // load and convert version 4 waypoint files
      if (header.waypoint_file_version == 4) {
         if (WaypointLoadVersion4(bfp, header.number_of_waypoints) == true) {
            fclose(bfp);
            return true;
         } else {
            fclose(bfp);
            return false;
         }
      }
      // make sure the waypoint version is compatible
      else if (header.waypoint_file_version != WAYPOINT_VERSION) {
         if (pEntity)
            ClientPrint(pEntity, HUD_PRINTNOTIFY, "Incompatible FoXBot waypoint file version!\nWaypoints not loaded!\n");

         if (IS_DEDICATED_SERVER())
            printf("Incompatible FoXBot waypoint file version!\nWaypoints not loaded!\n");

         fclose(bfp);
         return false;
      }

      WaypointInit(); // remove any existing waypoints

      // read the waypoint data from the file
      for (i = 0; i < header.number_of_waypoints; i++) {
         fread(&waypoints[i], sizeof(WAYPOINT), 1, bfp);
         ++num_waypoints;

         // keep track of which waypoint types have been loaded for each team
         // is this waypoint type for one team only?
         if (waypoints[i].flags & W_FL_TEAM_SPECIFIC) {
            if ((waypoints[i].flags & W_FL_TEAM) == 0)
               wp_type_exists[0] |= waypoints[i].flags;
            else if ((waypoints[i].flags & W_FL_TEAM) == 1)
               wp_type_exists[1] |= waypoints[i].flags;
            else if ((waypoints[i].flags & W_FL_TEAM) == 2)
               wp_type_exists[2] |= waypoints[i].flags;
            else if ((waypoints[i].flags & W_FL_TEAM) == 3)
               wp_type_exists[3] |= waypoints[i].flags;
         } else // it's available for all teams
         {
            wp_type_exists[0] |= waypoints[i].flags;
            wp_type_exists[1] |= waypoints[i].flags;
            wp_type_exists[2] |= waypoints[i].flags;
            wp_type_exists[3] |= waypoints[i].flags;
         }
      }

      // read and add waypoint paths...
      for (index = 0; index < num_waypoints; index++) {
         // read the number of paths from this node...
         fread(&num, sizeof num, 1, bfp);

         for (i = 0; i < num; i++) {
            fread(&path_index, sizeof path_index, 1, bfp);

            WaypointAddPath(index, path_index);
         }
      }

      g_waypoint_paths = true; // keep track so path can be freed

      // read waypoint authors name..if there is one (could be an old wpt file)
      fread(&waypoint_author, sizeof(char), 255, bfp);
      waypoint_author[254] = '\0';

      fclose(bfp);

      WaypointRouteInit();
      return true;
   } else {
      if (pEntity) {
         sprintf(msg, "Waypoint file %s doesn't exist!\nLooking for HPB file instead\n", filename);
         ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
      }

      if (IS_DEDICATED_SERVER())
         printf("waypoint file %s not found!\nLooking for HPB file instead\n", filename);
   }

   // try for hpb_bot file instead

   strcpy(mapname, STRING(gpGlobals->mapname));
   strcat(mapname, ".wpt");

   UTIL_BuildFileName(filename, 255, "waypoints", mapname);
   bfp = fopen(filename, "rb");

   // if file exists, read the waypoint structure from it
   if (bfp != nullptr) {
      if (IS_DEDICATED_SERVER())
         printf("loading waypoint file: %s\n", filename);
      fread(&header, sizeof header, 1, bfp);

      header.filetype[7] = 0;
      if (strcmp(header.filetype, "HPB_bot") == 0) {
         if (header.waypoint_file_version != WAYPOINT_VERSION) {
            if (pEntity)
               ClientPrint(pEntity, HUD_PRINTNOTIFY, "Incompatible HPB bot waypoint file version!\nWaypoints not loaded\n");

            fclose(bfp);
            return false;
         }

         header.mapname[31] = 0;

         if (strcmp(header.mapname, STRING(gpGlobals->mapname)) == 0) {
            WaypointInit(); // remove any existing waypoints

            for (i = 0; i < header.number_of_waypoints; i++) {
               fread(&waypoints[i], sizeof waypoints[0], 1, bfp);
               num_waypoints++;
            }

            // read and add waypoint paths...
            for (index = 0; index < num_waypoints; index++) {
               // read the number of paths from this node...
               fread(&num, sizeof num, 1, bfp);

               for (i = 0; i < num; i++) {
                  fread(&path_index, sizeof path_index, 1, bfp);

                  WaypointAddPath(index, path_index);
               }
            }

            g_waypoint_paths = true; // keep track so path can be freed
         } else {
            if (pEntity) {
               sprintf(msg, "%s HPB waypoints aren't for this map\n", filename);
               ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
            }

            fclose(bfp);
            return false;
         }
      } else {
         if (pEntity) {
            sprintf(msg, "%s is not a HPB waypoint file\n", filename);
            ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
         }

         fclose(bfp);
         return false;
      }

      fclose(bfp);

      WaypointSave();
      WaypointRouteInit();
   } else {
      if (pEntity) {
         sprintf(msg, "Waypoint file %s doesn't exist\n", filename);
         ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
      }

      if (IS_DEDICATED_SERVER())
         printf("waypoint file %s not found\n", filename);

      // attempt to generate temporary waypoints
      if (WaypointGenerateFromSpawn()) {
         if (pEntity)
            ClientPrint(pEntity, HUD_PRINTNOTIFY, "Temporary waypoints generated.\n");
         return true;
      }

      return false;
   }

   return true;
}

// This function will load the waypoints from a version 4 Foxbot waypoint file.
// Version 4 stores script flags in the main waypoint flag structure, version 5
// stores them in their own flag variable.
static bool WaypointLoadVersion4(FILE *bfp, const int number_of_waypoints) {
   // version 4 of the waypoint structure
   typedef struct {
      int flags;     // button, lift, flag, health, ammo, etc.
      Vector origin; // map location
   } WAYPOINT_VERSION4;

   WAYPOINT_VERSION4 dummy_waypoint;

   // these script flags used to be in the main waypoint flag
   constexpr int OLD_POINT1 = 1 << 16, OLD_POINT2 = 1 << 17, OLD_POINT3 = 1 << 18, OLD_POINT4 = 1 << 19, OLD_POINT5 = 1 << 20, OLD_POINT6 = 1 << 21, OLD_POINT7 = 1 << 22, OLD_POINT8 = 1 << 23;

   int i;
   short int num;
   short int path_index;

   WaypointInit(); // remove any existing waypoints

   // read the waypoint data from the file
   for (i = 0; i < number_of_waypoints; i++) {
      fread(&dummy_waypoint, sizeof(WAYPOINT_VERSION4), 1, bfp);

      // convert version 4 data to version 5 data

      waypoints[i].origin = dummy_waypoint.origin;

      // move all script flags over to their new home
      if (dummy_waypoint.flags & OLD_POINT1)
         waypoints[i].script_flags |= S_FL_POINT1;
      if (dummy_waypoint.flags & OLD_POINT2)
         waypoints[i].script_flags |= S_FL_POINT2;
      if (dummy_waypoint.flags & OLD_POINT3)
         waypoints[i].script_flags |= S_FL_POINT3;
      if (dummy_waypoint.flags & OLD_POINT4)
         waypoints[i].script_flags |= S_FL_POINT4;
      if (dummy_waypoint.flags & OLD_POINT5)
         waypoints[i].script_flags |= S_FL_POINT5;
      if (dummy_waypoint.flags & OLD_POINT6)
         waypoints[i].script_flags |= S_FL_POINT6;
      if (dummy_waypoint.flags & OLD_POINT7)
         waypoints[i].script_flags |= S_FL_POINT7;
      if (dummy_waypoint.flags & OLD_POINT8)
         waypoints[i].script_flags |= S_FL_POINT8;

      // the script flags have been moved, so clear their old home
      dummy_waypoint.flags &= ~(OLD_POINT1 | OLD_POINT2 | OLD_POINT3 | OLD_POINT4 | OLD_POINT5 | OLD_POINT6 | OLD_POINT7 | OLD_POINT8);

      waypoints[i].flags = dummy_waypoint.flags;

      ++num_waypoints;

      // keep track of which waypoint types have been loaded for each team
      // is this waypoint type for one team only?
      if (waypoints[i].flags & W_FL_TEAM_SPECIFIC) {
         if ((waypoints[i].flags & W_FL_TEAM) == 0)
            wp_type_exists[0] |= waypoints[i].flags;
         else if ((waypoints[i].flags & W_FL_TEAM) == 1)
            wp_type_exists[1] |= waypoints[i].flags;
         else if ((waypoints[i].flags & W_FL_TEAM) == 2)
            wp_type_exists[2] |= waypoints[i].flags;
         else if ((waypoints[i].flags & W_FL_TEAM) == 3)
            wp_type_exists[3] |= waypoints[i].flags;
      } else // it's available for all teams
      {
         wp_type_exists[0] |= waypoints[i].flags;
         wp_type_exists[1] |= waypoints[i].flags;
         wp_type_exists[2] |= waypoints[i].flags;
         wp_type_exists[3] |= waypoints[i].flags;
      }
   }

   // read and add waypoint paths...
   for (int index = 0; index < num_waypoints; index++) {
      // read the number of paths from this node...
      fread(&num, sizeof num, 1, bfp);

      for (i = 0; i < num; i++) {
         fread(&path_index, sizeof path_index, 1, bfp);

         WaypointAddPath(index, path_index);
      }
   }

   g_waypoint_paths = true; // keep track so path can be freed

   // read waypoint authors name...if there is one (could be an old wpt file)
   fread(&waypoint_author, sizeof(char), 255, bfp);
   waypoint_author[254] = '\0';

   WaypointRouteInit();
   return true;
}

// this function is run when you type 'waypoint save' in the console
void WaypointSave() {
   char filename[256];
   char mapname[64];
   WAYPOINT_HDR header;

   strcpy(header.filetype, "FoXBot");
   header.waypoint_file_version = WAYPOINT_VERSION;
   header.waypoint_file_flags = 0; // not currently used
   header.number_of_waypoints = num_waypoints;

   memset(header.mapname, 0, sizeof header.mapname);
   strncpy(header.mapname, STRING(gpGlobals->mapname), 31);
   header.mapname[31] = 0;

   strcpy(mapname, STRING(gpGlobals->mapname));
   strcat(mapname, ".fwp");

   UTIL_BuildFileName(filename, 255, "waypoints", mapname);
   FILE *bfp = fopen(filename, "wb");

   if (bfp == nullptr) {
      ALERT(at_console, "Couldn't open a waypoint file to save waypoint data into.\n");
      return;
   }

   // write the waypoint header to the file...
   fwrite(&header, sizeof header, 1, bfp);

   int index, i;
   short int num;

   // write the waypoint data to the file...
   for (index = 0; index < num_waypoints; index++) {
      fwrite(&waypoints[index], sizeof waypoints[0], 1, bfp);
   }

   // save the waypoint paths...
   for (index = 0; index < num_waypoints; index++) {
      // count the number of paths from this node...

      PATH *p = paths[index];
      num = 0;

      while (p != nullptr) {
         i = 0;

         while (i < MAX_PATH_INDEX) {
            if (p->index[i] != -1)
               num++; // count path node if it's used

            i++;
         }

         p = p->next; // go to next node in linked list
      }

      fwrite(&num, sizeof num, 1, bfp); // write the count

      // now write out each path index...

      p = paths[index];

      while (p != nullptr) {
         i = 0;

         while (i < MAX_PATH_INDEX) {
            if (p->index[i] != -1) // save path node if it's used
               fwrite(&p->index[i], sizeof p->index[0], 1, bfp);

            i++;
         }

         p = p->next; // go to next node in linked list
      }
   }

   // write waypoint authors name..if there is one
   // (could be an old waypoint file)
   fwrite(&waypoint_author, sizeof(char), 255, bfp);

   fclose(bfp);
}

bool WaypointCacheSave() {
   char filename[256];
   char mapname[64];
   WAYPOINT_HDR header;

   strcpy(header.filetype, "FoXBot");
   header.waypoint_file_version = WAYPOINT_VERSION;
   header.waypoint_file_flags = 0;
   header.number_of_waypoints = num_waypoints;

   memset(header.mapname, 0, sizeof header.mapname);
   strncpy(header.mapname, STRING(gpGlobals->mapname), 31);
   header.mapname[31] = 0;

   strncpy(mapname, STRING(gpGlobals->mapname), sizeof(mapname) - 1);
   mapname[sizeof(mapname) - 1] = '\0';
   strncat(mapname, "_cache.fwp", sizeof(mapname) - strlen(mapname) - 1);

   UTIL_BuildFileName(filename, 255, "mapdata", mapname);
   FILE *bfp = fopen(filename, "wb");
   if (bfp == nullptr) {
      ALERT(at_error, "WaypointCacheSave: failed to open %s\n", filename);
      return false;
   }

   fwrite(&header, sizeof header, 1, bfp);

   for (int index = 0; index < num_waypoints; index++)
      fwrite(&waypoints[index], sizeof waypoints[0], 1, bfp);

   for (int index = 0; index < num_waypoints; index++) {
      PATH *p = paths[index];
      short int num = 0;
      while (p != nullptr) {
         for (int i = 0; i < MAX_PATH_INDEX; i++)
            if (p->index[i] != -1)
               ++num;
         p = p->next;
      }
      fwrite(&num, sizeof num, 1, bfp);
      p = paths[index];
      while (p != nullptr) {
         for (int i = 0; i < MAX_PATH_INDEX; i++)
            if (p->index[i] != -1)
               fwrite(&p->index[i], sizeof p->index[0], 1, bfp);
         p = p->next;
      }
   }

   fwrite(&waypoint_author, sizeof(char), 255, bfp);
   fclose(bfp);
   return true;
}

bool WaypointCacheLoad() {
   char filename[256];
   char mapname[64];
   WAYPOINT_HDR header;

   strncpy(mapname, STRING(gpGlobals->mapname), sizeof(mapname) - 1);
   mapname[sizeof(mapname) - 1] = '\0';
   strncat(mapname, "_cache.fwp", sizeof(mapname) - strlen(mapname) - 1);
   UTIL_BuildFileName(filename, 255, "mapdata", mapname);
   FILE *bfp = fopen(filename, "rb");
   if (bfp == nullptr) {
      ALERT(at_console, "WaypointCacheLoad: %s not found\n", filename);
      return false;
   }

   fread(&header, sizeof header, 1, bfp);
   header.filetype[7] = 0;
   if (strcmp(header.filetype, "FoXBot") != 0 ||
       header.waypoint_file_version != WAYPOINT_VERSION ||
       strcasecmp(header.mapname, STRING(gpGlobals->mapname)) != 0) {
      fclose(bfp);
      return false;
   }

   WaypointInit();
   num_waypoints = 0;
   for (int i = 0; i < header.number_of_waypoints && i < MAX_WAYPOINTS; i++) {
      fread(&waypoints[i], sizeof(WAYPOINT), 1, bfp);
      ++num_waypoints;
   }

   for (int index = 0; index < num_waypoints; index++) {
      short int num = 0;
      short int path_index = 0;
      fread(&num, sizeof num, 1, bfp);
      for (int i = 0; i < num; i++) {
         fread(&path_index, sizeof path_index, 1, bfp);
         WaypointAddPath(index, path_index);
      }
   }

   g_waypoint_paths = true;
   fread(&waypoint_author, sizeof(char), 255, bfp);
   waypoint_author[254] = '\0';
   fclose(bfp);
   WaypointRouteInit();
   return true;
}

// This function can check to see if bots will be able to get from one
// waypoint to another.
bool WaypointReachable(Vector v_src, Vector v_dest, const edict_t *pEntity) {
   float distance = (v_dest - v_src).Length();

   // is the destination close enough?
   if (distance < REACHABLE_RANGE) {
      TraceResult tr;

      // check if this waypoint is "visible"...

      UTIL_TraceLine(v_src, v_dest, ignore_monsters, pEntity->v.pContainingEntity, &tr);

      // if waypoint is visible from current position
      // (even behind head)...
      if (tr.flFraction >= 1.0) {
         // check for special case of both waypoints being underwater...
         if (POINT_CONTENTS(v_src) == CONTENTS_WATER && POINT_CONTENTS(v_dest) == CONTENTS_WATER)
            return true;

         // check for special case of waypoint being suspended in mid-air...

         // is dest waypoint higher than src? (45 is max jump height)
         if (v_dest.z > v_src.z + 45.0) {
            const Vector &v_new_src = v_dest;
            Vector v_new_dest = v_dest;

            v_new_dest.z = v_new_dest.z - 50; // straight down 50 units

            UTIL_TraceLine(v_new_src, v_new_dest, dont_ignore_monsters, pEntity->v.pContainingEntity, &tr);

            // check if we didn't hit anything,
            // if not then it's in mid-air
            if (tr.flFraction >= 1.0) {
               return false; // can't reach this one
            }
         }

         // check if distance to ground increases more than jump height
         // at points between source and destination...

         const Vector v_direction = (v_dest - v_src).Normalize(); // 1 unit long
         Vector v_check = v_src;
         Vector v_down = v_src;

         v_down.z = v_down.z - 1000.0; // straight down 1000 units

         UTIL_TraceLine(v_check, v_down, ignore_monsters, pEntity->v.pContainingEntity, &tr);

         float last_height = tr.flFraction * 1000.0; // height from ground

         distance = (v_dest - v_check).Length(); // distance from goal

         while (distance > 10.0) {
            // move 10 units closer to the goal...
            v_check = v_check + v_direction * 10.0;

            v_down = v_check;
            v_down.z = v_down.z - 1000.0; // straight down 1000 units

            UTIL_TraceLine(v_check, v_down, ignore_monsters, pEntity->v.pContainingEntity, &tr);

            const float curr_height = tr.flFraction * 1000.0; // height from ground

            // is the difference in the last height and the current
            // height higher that the jump height?
            if (last_height - curr_height > 45.0) {
               // can't get there from here...
               return false;
            }

            last_height = curr_height;

            distance = (v_dest - v_check).Length(); // distance from goal
         }

         return true;
      }
   }

   return false;
}

// This function returns true if the source waypoint has a path connected
// directly to the destination waypoint.
bool WaypointDirectPathCheck(const int srcWP, const int destWP) {
   // sanity check
   if (srcWP == -1)
      return false;

   // sanity check
   if (srcWP == destWP)
      return true;

   PATH *p = paths[srcWP];

   while (p != nullptr) {
      for (int i = 0; i < MAX_PATH_INDEX; i++) {
         if (p->index[i] == destWP)
            return true;
      }

      p = p->next; // go to next node in linked list
   }

   return false;
}

// find the nearest reachable waypoint
// Returns -1 if there wasn't one near enough
// currently, this function is unused
/*int WaypointFindReachable(edict_t *pEntity, const float range,
                const int team)
{
                int i, min_index = -1;
                float distance;  //distance to the closest waypoint found so far
                float min_distance = range + 1.0f;  //min_distance should be greater than range at start
                TraceResult tr;

                // find the nearest waypoint...
                for(i = 0; i < num_waypoints; i++)
                {
                                // DrEvils rocketjump pathing
                                if((pEntity->v.playerclass != TFC_CLASS_SOLDIER)
                                                && (waypoints[i].flags & W_FL_TFC_JUMP))
                                                continue;

                                if(waypoints[i].flags & W_FL_DELETED)
                                                continue;  // skip any deleted waypoints

                                if(waypoints[i].flags & W_FL_AIMING)
                                                continue;  // skip any aiming waypoints

                                // skip this waypoint if it's team specific and teams don't match
                                if(team != -1
                                                && waypoints[i].flags & W_FL_TEAM_SPECIFIC
                                                && (waypoints[i].flags & W_FL_TEAM) != team)
                                                continue;

                                distance = (waypoints[i].origin - pEntity->v.origin).Length();

                                if(distance < min_distance)
                                {
                                                // if waypoint is visible from current position
                                                // (even behind head)...
                                                UTIL_TraceLine( pEntity->v.origin + pEntity->v.view_ofs,
                                                                waypoints[i].origin,	ignore_monsters,
                                                                pEntity->v.pContainingEntity, &tr );

                                                if(tr.flFraction >= 1.0)
                                                {
                                                                if(WaypointReachable(pEntity->v.origin, waypoints[i].origin, pEntity))
                                                                {
                                                                                min_index = i;
                                                                                min_distance = distance;
                                                                }
                                                }
                                }
                }

                return min_index;
}*/

// This function is called when the player types in the console
// command: 'waypoint info'
void WaypointPrintInfo(edict_t *pEntity) {
   char msg[96];

   // find the nearest waypoint...
   const int index = WaypointFindNearest_E(pEntity, 50.0, -1);

   if (index == -1)
      return;

   // find out how many waypoints in the list are deleted
   int deleted_waypoint_total = 0;
   for (int i = 0; i < num_waypoints; i++) {
      if (waypoints[i].flags & W_FL_DELETED)
         ++deleted_waypoint_total;
   }

   // spit out some basic info first
   snprintf(msg, 95, "Waypoint %d of %d total.\n%d deleted waypoint indexes can be re-used.\n", index, num_waypoints, deleted_waypoint_total);
   ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);

   const WPT_INT32 flags = waypoints[index].flags;

   // now to provide more specific info...

   if (flags & W_FL_TEAM_SPECIFIC) {
      if ((flags & W_FL_TEAM) == 0)
         strcpy(msg, "Waypoint is for TEAM 1\n");
      else if ((flags & W_FL_TEAM) == 1)
         strcpy(msg, "Waypoint is for TEAM 2\n");
      else if ((flags & W_FL_TEAM) == 2)
         strcpy(msg, "Waypoint is for TEAM 3\n");
      else if ((flags & W_FL_TEAM) == 3)
         strcpy(msg, "Waypoint is for TEAM 4\n");
      else
         strcpy(msg, ""); // shouldn't happen

      ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
   }

   if (flags & W_FL_LIFT)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "Bot will wait for lift before approaching\n");

   if (flags & W_FL_LADDER)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This waypoint is on a ladder\n");

   if (flags & W_FL_WALK)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a walk waypoint\n");

   if (flags & W_FL_CROUCH)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a crouch waypoint\n");

   if (flags & W_FL_HEALTH)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "There is health near this waypoint\n");

   if (flags & W_FL_ARMOR)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "There is armor near this waypoint\n");

   if (flags & W_FL_AMMO)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "There is ammo near this waypoint\n");

   if (flags & W_FL_SNIPER)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a sniper waypoint\n");

   if (flags & W_FL_JUMP)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a jump waypoint\n");

   if (flags & W_FL_TFC_SENTRY)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a sentry waypoint\n");

   if (flags & W_FL_TFC_SENTRY_180)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a 180 degree sentry rotation waypoint\n");

   if (flags & W_FL_TFC_TELEPORTER_ENTRANCE)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a teleporter entrance waypoint\n");

   if (flags & W_FL_TFC_TELEPORTER_EXIT)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a teleporter exit waypoint\n");

   if (flags & W_FL_TFC_PL_DEFEND)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a HW/Soldier defender waypoint\n");

   if (flags & W_FL_TFC_PIPETRAP)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a Demo-man defender waypoint\n");

   if (flags & W_FL_TFC_DETPACK_CLEAR)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a Demo-man det-pack waypoint for clearing passageways\n");

   if (flags & W_FL_TFC_DETPACK_SEAL)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a det-pack waypoint for sealing passageways\n");

   if (flags & W_FL_PATHCHECK)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a path check waypoint\n");

   if (flags & W_FL_TFC_JUMP)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is a rocket/concussion jump waypoint\n");

   if (flags & W_FL_TFC_FLAG)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "There is a flag near this waypoint\n");

   if (flags & W_FL_TFC_FLAG_GOAL)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "There is a flag goal near this waypoint\n");

   // script flags next

   if (waypoints[index].script_flags & S_FL_POINT1)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point1\n");

   if (waypoints[index].script_flags & S_FL_POINT2)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point2\n");

   if (waypoints[index].script_flags & S_FL_POINT3)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point3\n");

   if (waypoints[index].script_flags & S_FL_POINT4)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point4\n");

   if (waypoints[index].script_flags & S_FL_POINT5)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point5\n");

   if (waypoints[index].script_flags & S_FL_POINT6)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point6\n");

   if (waypoints[index].script_flags & S_FL_POINT7)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point7\n");

   if (waypoints[index].script_flags & S_FL_POINT8)
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point8\n");

   /*
   if (flags & W_FL_POINT1)
           ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point1\n");

   if (flags & W_FL_POINT2)
           ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point2\n");

   if (flags & W_FL_POINT3)
           ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point3\n");

   if (flags & W_FL_POINT4)
           ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point4\n");

   if (flags & W_FL_POINT5)
           ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point5\n");

   if (flags & W_FL_POINT6)
           ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point6\n");

   if (flags & W_FL_POINT7)
           ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point7\n");

   if (flags & W_FL_POINT8)
           ClientPrint(pEntity, HUD_PRINTNOTIFY, "This is marked as point8\n");
   */
}

void WaypointThink(edict_t *pEntity) {
   // crashes??
   if (!pEntity || botcamEnabled)
      return;

   float distance, min_distance;
   Vector start, end;
   int i;

   // If auto waypoint is on then auto waypoint as u run.
   if (g_auto_waypoint && !(pEntity->v.flags & FL_FAKECLIENT)) // and not a bot
   {
      // find the distance from the last used waypoint
      distance = (last_waypoint - pEntity->v.origin).Length();

      if (distance > 200.0f) {
         min_distance = 9999.0f;

         // check that no other reachable waypoints are nearby...
         for (i = 0; i < num_waypoints; i++) {
            if (waypoints[i].flags & W_FL_DELETED || waypoints[i].flags & W_FL_AIMING)
               continue;

            if (WaypointReachable(pEntity->v.origin, waypoints[i].origin, pEntity)) {
               distance = (waypoints[i].origin - pEntity->v.origin).Length();

               if (distance < min_distance)
                  min_distance = distance;
            }
         }

         // make sure nearest waypoint is far enough away...
         if (min_distance >= 200.0f)
            WaypointAdd(pEntity); // place a waypoint here
      }
   }

   min_distance = 9999.0f;

   if ((g_waypoint_on || g_area_def) && !g_waypoint_cache) {
      char cmd[255];
      sprintf(cmd, "changelevel %s\n", STRING(gpGlobals->mapname));
      SERVER_COMMAND(cmd);
   }

   // display the waypoints if turned on...
   if (g_waypoint_on && g_waypoint_cache && !(pEntity->v.flags & FL_FAKECLIENT)) // and not a bot
   {
      int index = -1;

      if (g_path_oneway)
         WaypointRunOneWay(pEntity);
      else if (g_path_twoway)
         WaypointRunTwoWay(pEntity);

      for (i = 0; i < num_waypoints; i++) {
         if (waypoints[i].flags & W_FL_DELETED)
            continue;

         distance = (waypoints[i].origin - pEntity->v.origin).Length();
         if (distance < 200.0f) {
            // waypoint holographic models + sprites
            if (pEntity == INDEXENT(1)) {
               WPT_INT32 flags = waypoints[i].flags;
               if (flags & W_FL_HEALTH || flags & W_FL_ARMOR || flags & W_FL_AMMO || flags & W_FL_SNIPER || flags & W_FL_TFC_FLAG || flags & W_FL_TFC_FLAG_GOAL || flags & W_FL_TFC_SENTRY || flags & W_FL_TFC_TELEPORTER_ENTRANCE ||
                   flags & W_FL_TFC_TELEPORTER_EXIT || flags & W_FL_LIFT || flags & W_FL_JUMP || flags & W_FL_TFC_DETPACK_CLEAR || flags & W_FL_TFC_DETPACK_SEAL || flags & W_FL_TFC_JUMP || flags & W_FL_TFC_PL_DEFEND ||
                   flags & W_FL_TFC_PIPETRAP || flags & W_FL_PATHCHECK) {
                  short model;
                  if (flags & W_FL_HEALTH) {
                     model = PRECACHE_MODEL("models/w_medkit.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(64);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_ARMOR) {
                     model = PRECACHE_MODEL("models/r_armor.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_AMMO) {
                     model = PRECACHE_MODEL("models/backpack.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_SNIPER) {
                     model = PRECACHE_MODEL("models/p_sniper.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_TFC_FLAG) {
                     model = PRECACHE_MODEL("models/flag.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_TFC_FLAG_GOAL) {
                     model = PRECACHE_MODEL("models/bigrat.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(64);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_TFC_SENTRY) {
                     model = PRECACHE_MODEL("models/sentry1.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_TFC_PL_DEFEND) {
                     model = PRECACHE_MODEL("models/player/hvyweapon/hvyweapon.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_TFC_PIPETRAP) {
                     model = PRECACHE_MODEL("models/player/demo/demo.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_JUMP) {
                     model = PRECACHE_MODEL("models/w_longjump.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_TFC_JUMP) {
                     model = PRECACHE_MODEL("models/rpgrocket.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_TFC_DETPACK_CLEAR) {
                     model = PRECACHE_MODEL("models/detpack.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_TFC_DETPACK_SEAL) {
                     model = PRECACHE_MODEL("models/detpack.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z + 25); // higher than normal
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();

                     // draw 2 crossed lines to create a blockage symbol
                     if (wp_display_time[i] + 1.0 < gpGlobals->time) {
                        Vector beam_start = waypoints[i].origin + Vector(20, 0, 20);
                        Vector beam_end = waypoints[i].origin - Vector(20, 0, 0) - Vector(0, 0, 20);
                        WaypointDrawBeam(pEntity, beam_start, beam_end, 10, 2, 250, 0, 250, 200, 10);

                        beam_start = waypoints[i].origin + Vector(0, 20, 20);
                        beam_end = waypoints[i].origin - Vector(0, 20, 0) - Vector(0, 0, 20);
                        WaypointDrawBeam(pEntity, beam_start, beam_end, 10, 2, 250, 0, 250, 200, 10);
                     }
                  }
                  if (flags & W_FL_TFC_TELEPORTER_ENTRANCE || flags & W_FL_TFC_TELEPORTER_EXIT) {
                     model = PRECACHE_MODEL("models/teleporter.mdl");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_LIFT) {
                     model = PRECACHE_MODEL("sprites/arrow1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(0.1);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (flags & W_FL_PATHCHECK && wp_display_time[i] + 1.0 < gpGlobals->time) {
                     // draw 2 crossed lines to create a signpost symbol
                     // near the top of the waypoint
                     Vector beam_start = waypoints[i].origin + Vector(15, 0, 25);
                     Vector beam_end = waypoints[i].origin - Vector(15, 0, 0) + Vector(0, 0, 25);
                     WaypointDrawBeam(pEntity, beam_start, beam_end, 10, 2, 250, 250, 250, 200, 10);

                     beam_start = waypoints[i].origin + Vector(0, 15, 25);
                     beam_end = waypoints[i].origin - Vector(0, 15, 0) + Vector(0, 0, 25);
                     WaypointDrawBeam(pEntity, beam_start, beam_end, 10, 2, 250, 250, 250, 200, 10);
                  }
               }

               // display dots showing what script flags are set for a waypoint //Incompatible for AvD maps? [APG]RoboCop[CL]
               WPT_INT8 script_flags = waypoints[i].script_flags;
               if (script_flags & S_FL_POINT1 || script_flags & S_FL_POINT2 || script_flags & S_FL_POINT3 || script_flags & S_FL_POINT4 || script_flags & S_FL_POINT5 || script_flags & S_FL_POINT6 || script_flags & S_FL_POINT7 ||
                   script_flags & S_FL_POINT8) {
                  short model;
                  if (script_flags & S_FL_POINT1) {
                     model = PRECACHE_MODEL("sprites/cnt1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z + 40);
                     WRITE_SHORT(model);
                     WRITE_BYTE(2);
                     WRITE_BYTE(64);
                     MESSAGE_END();
                  } else {
                     model = PRECACHE_MODEL("sprites/gargeye1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z + 40);
                     WRITE_SHORT(model);
                     WRITE_BYTE(3);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (script_flags & S_FL_POINT2) {
                     model = PRECACHE_MODEL("sprites/cnt1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z + 30);
                     WRITE_SHORT(model);
                     WRITE_BYTE(2);
                     WRITE_BYTE(64);
                     MESSAGE_END();
                  } else {
                     model = PRECACHE_MODEL("sprites/gargeye1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z + 30);
                     WRITE_SHORT(model);
                     WRITE_BYTE(3);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (script_flags & S_FL_POINT3) {
                     model = PRECACHE_MODEL("sprites/cnt1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z + 20);
                     WRITE_SHORT(model);
                     WRITE_BYTE(2);
                     WRITE_BYTE(64);
                     MESSAGE_END();
                  } else {
                     model = PRECACHE_MODEL("sprites/gargeye1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z + 20);
                     WRITE_SHORT(model);
                     WRITE_BYTE(3);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (script_flags & S_FL_POINT4) {
                     model = PRECACHE_MODEL("sprites/cnt1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z + 10);
                     WRITE_SHORT(model);
                     WRITE_BYTE(2);
                     WRITE_BYTE(64);
                     MESSAGE_END();
                  } else {
                     model = PRECACHE_MODEL("sprites/gargeye1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z + 10);
                     WRITE_SHORT(model);
                     WRITE_BYTE(3);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (script_flags & S_FL_POINT5) {
                     model = PRECACHE_MODEL("sprites/cnt1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(2);
                     WRITE_BYTE(64);
                     MESSAGE_END();
                  } else {
                     model = PRECACHE_MODEL("sprites/gargeye1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z);
                     WRITE_SHORT(model);
                     WRITE_BYTE(3);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (script_flags & S_FL_POINT6) {
                     model = PRECACHE_MODEL("sprites/cnt1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z - 10);
                     WRITE_SHORT(model);
                     WRITE_BYTE(2);
                     WRITE_BYTE(64);
                     MESSAGE_END();
                  } else {
                     model = PRECACHE_MODEL("sprites/gargeye1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z - 10);
                     WRITE_SHORT(model);
                     WRITE_BYTE(3);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (script_flags & S_FL_POINT7) {
                     model = PRECACHE_MODEL("sprites/cnt1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z - 20);
                     WRITE_SHORT(model);
                     WRITE_BYTE(2);
                     WRITE_BYTE(64);
                     MESSAGE_END();
                  } else {
                     model = PRECACHE_MODEL("sprites/gargeye1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z - 20);
                     WRITE_SHORT(model);
                     WRITE_BYTE(3);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
                  if (script_flags & S_FL_POINT8) {
                     model = PRECACHE_MODEL("sprites/cnt1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z - 30);
                     WRITE_SHORT(model);
                     WRITE_BYTE(2);
                     WRITE_BYTE(64);
                     MESSAGE_END();
                  } else {
                     model = PRECACHE_MODEL("sprites/gargeye1.spr");
                     MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pEntity->v.origin);
                     WRITE_BYTE(17);
                     WRITE_COORD(waypoints[i].origin.x);
                     WRITE_COORD(waypoints[i].origin.y);
                     WRITE_COORD(waypoints[i].origin.z - 30);
                     WRITE_SHORT(model);
                     WRITE_BYTE(3);
                     WRITE_BYTE(128);
                     MESSAGE_END();
                  }
               }
            }

            if (distance < min_distance) {
               index = i; // store index of nearest waypoint
               min_distance = distance;
            }

            if (wp_display_time[i] + 1.0 < gpGlobals->time) {
               if (waypoints[i].flags & W_FL_CROUCH) {
                  start = waypoints[i].origin - Vector(0, 0, 17);
                  end = start + Vector(0, 0, 34);
               } else if (waypoints[i].flags & W_FL_AIMING) {
                  start = waypoints[i].origin + Vector(0, 0, 10);
                  end = start + Vector(0, 0, 14);
               } else {
                  start = waypoints[i].origin - Vector(0, 0, 34);
                  end = start + Vector(0, 0, 68);
               }

               // draw the waypoint
               int r = 150, g = 150, b = 150;
               if (waypoints[i].flags & W_FL_TEAM_SPECIFIC) {
                  if ((waypoints[i].flags & W_FL_TEAM) == 0) {
                     r = 0;
                     g = 0;
                     b = 255;
                  }
                  if ((waypoints[i].flags & W_FL_TEAM) == 1) {
                     r = 255;
                     g = 0;
                     b = 0;
                  }
                  if ((waypoints[i].flags & W_FL_TEAM) == 2) {
                     r = 255;
                     g = 255;
                     b = 0; // yellow team
                  }
                  if ((waypoints[i].flags & W_FL_TEAM) == 3) {
                     r = 0;
                     g = 255;
                     b = 0;
                  }
               }
               WaypointDrawBeam(pEntity, start, end, 30, 0, r, g, b, 250, 5);

               // ok, last but not least, jump and lift waypoints..

               if (is_junction[i]) {
                  WaypointDrawBeam(pEntity, end + Vector(0, -10, 20), end + Vector(0, 10, 20), 30, 0, 255, 255, 255, 250, 5);
                  WaypointDrawBeam(pEntity, end + Vector(-10, 0, 20), end + Vector(10, 0, 20), 30, 0, 255, 255, 255, 250, 5);
               }

               wp_display_time[i] = gpGlobals->time;
            }
         }
      }
      // end of for loop...

      // check if path waypointing is on...
      // check if player is close enough to a waypoint and
      // time to draw path...
      if (g_path_waypoint && index != -1 && min_distance <= 50 && f_path_time <= gpGlobals->time) {
         f_path_time = gpGlobals->time + 1.0f;

         PATH *p;

         // reverse paths...
         // paths to the one you're standing at..
         for (int ii = 0; ii < num_waypoints; ii++) {
            // only include ones close enough
            if ((waypoints[index].origin - waypoints[ii].origin).Length() < 800) {
               p = paths[ii];
               while (p != nullptr) {
                  i = 0;
                  while (i < MAX_PATH_INDEX) {
                     // goes to waypoint were standing at
                     if (p->index[i] != -1 && p->index[i] == index) {
                        Vector v_src = waypoints[ii].origin - Vector(0, 0, 16);
                        Vector v_dest = waypoints[p->index[i]].origin - Vector(0, 0, 16);
                        WaypointDrawBeam(pEntity, v_src, v_dest, 10, 2, 200, 200, 0, 200, 10);
                     }
                     i++;
                  }
                  p = p->next; // go to next node in linked list
               }
            }
         }

         p = paths[index];

         while (p != nullptr) {
            i = 0;

            while (i < MAX_PATH_INDEX) {
               if (p->index[i] != -1) {
                  Vector v_src = waypoints[index].origin;
                  Vector v_dest = waypoints[p->index[i]].origin;

                  // draw a white line to this index's waypoint
                  WaypointDrawBeam(pEntity, v_src, v_dest, 10, 2, 250, 250, 250, 200, 10);
               }

               i++;
            }

            p = p->next; // go to next node in linked list
         }
      }
   }

   // mdls=g_waypoint_on;
   // my debug stuff
   if (g_find_waypoint && !(pEntity->v.flags & FL_FAKECLIENT)) {
      WaypointDrawBeam(pEntity, pEntity->v.origin, waypoints[g_find_wp].origin, 10, 2, 250, 250, 0, 200, 10);

      // provide info about how far away the waypoint is
      if (!IS_DEDICATED_SERVER()) {
         edict_t *pPlayer = INDEXENT(1);

         // try to find out the route distance to the waypoint
         int nearWP = WaypointFindNearest_E(pEntity, 50.0, -1);
         int routeDistance = WaypointDistanceFromTo(nearWP, g_find_wp, -1);

         char msg[128];
         snprintf(msg, 128, "Waypoint height difference: %f\ndistance: %f\nroute distance %d", waypoints[g_find_wp].origin.z - pPlayer->v.origin.z, (waypoints[g_find_wp].origin - pPlayer->v.origin).Length(), routeDistance);
         msg[127] = '\0';

         MESSAGE_BEGIN(MSG_ONE, SVC_TEMPENTITY, nullptr, pPlayer);
         WRITE_BYTE(TE_TEXTMESSAGE);
         WRITE_BYTE(2 & 0xFF);
         WRITE_SHORT(-12500);
         WRITE_SHORT(-8192);
         WRITE_BYTE(1); // effect

         WRITE_BYTE(255);
         WRITE_BYTE(255);
         WRITE_BYTE(255);
         WRITE_BYTE(255);

         WRITE_BYTE(255);
         WRITE_BYTE(255);
         WRITE_BYTE(255);
         WRITE_BYTE(255);
         WRITE_SHORT(0);   // fade-in time
         WRITE_SHORT(0);   // fade-out time
         WRITE_SHORT(255); // hold time

         WRITE_STRING(msg);
         MESSAGE_END();
      }
   }

   // area def draw
   //	min_distance = 9999.0;
   if (g_area_def && g_waypoint_cache && !(pEntity->v.flags & FL_FAKECLIENT)) {
      bool timr;
      for (i = 0; i < num_areas; i++) {
         // checks only for whole area delete.....
         if ((areas[i].flags & W_FL_DELETED) == W_FL_DELETED)
            continue;
         timr = false; // Fix for AvD maps? [APG]RoboCop[CL]
         if ((areas[i].flags & A_FL_1) == A_FL_1) {
            // display 1 of 4..(a)
            distance = (areas[i].a - pEntity->v.origin).Length();

            if (distance < 300) {
               //	if(distance < min_distance)
               //	{
               //	index = i; // store index of nearest waypoint
               //		min_distance = distance;
               //	}

               if (a_display_time[i] + 1.0 < gpGlobals->time) {
                  start = areas[i].a - Vector(0, 0, 34);
                  end = start + Vector(0, 0, 68);
                  // draw a red waypoint
                  WaypointDrawBeam(pEntity, start, end, 30, 0, 255, 0, 0, 250, 5);

                  timr = true;
               }
            }
         }
         if ((areas[i].flags & A_FL_2) == A_FL_2) {
            // display 2 of 4..(b)
            distance = (areas[i].b - pEntity->v.origin).Length();

            if (distance < 300) {
               //	if(distance < min_distance)
               //	{
               //	index = i; // store index of nearest waypoint
               //		min_distance = distance;
               //	}

               if (a_display_time[i] + 1.0 < gpGlobals->time) {
                  start = areas[i].b - Vector(0, 0, 34);
                  end = start + Vector(0, 0, 68);
                  // draw a red waypoint
                  WaypointDrawBeam(pEntity, start, end, 30, 0, 255, 0, 0, 250, 5);

                  timr = true;
               }
            }
         }
         if ((areas[i].flags & A_FL_3) == A_FL_3) {
            // display 3 of 4..(c)
            distance = (areas[i].c - pEntity->v.origin).Length();

            if (distance < 300) {
               //	if(distance < min_distance)
               //	{
               //	index = i; // store index of nearest waypoint
               //		min_distance = distance;
               //	}

               if (a_display_time[i] + 1.0 < gpGlobals->time) {
                  start = areas[i].c - Vector(0, 0, 34);
                  end = start + Vector(0, 0, 68);
                  // draw a red waypoint
                  WaypointDrawBeam(pEntity, start, end, 30, 0, 255, 0, 0, 250, 5);

                  timr = true;
               }
            }
         }
         if ((areas[i].flags & A_FL_4) == A_FL_4) {
            // display 4 of 4..(d)
            distance = (areas[i].d - pEntity->v.origin).Length();

            if (distance < 300) {
               //	if(distance < min_distance)
               //	{
               //	index = i; // store index of nearest waypoint
               //		min_distance = distance;
               //	}

               if (a_display_time[i] + 1.0 < gpGlobals->time) {
                  start = areas[i].d - Vector(0, 0, 34);
                  end = start + Vector(0, 0, 68);
                  // draw a red waypoint
                  WaypointDrawBeam(pEntity, start, end, 30, 0, 255, 0, 0, 250, 5);

                  timr = true;
               }
            }
         }

         // connect the dots :)
         if (a_display_time[i] + 1.0 < gpGlobals->time && timr) {
            int g = 0;
            // if(AreaInside(pEntity,i)) g=64;
            if (AreaInsideClosest(pEntity) == i) {
               g = 255;
               if (last_area != i && pEntity == INDEXENT(1)) {
                  AreaDefPrintInfo(pEntity);
                  last_area = i;
               }
            }
            if ((areas[i].flags & A_FL_1) == A_FL_1 && (areas[i].flags & A_FL_2) == A_FL_2)
               WaypointDrawBeam(pEntity, areas[i].a + Vector(0, 0, 34), areas[i].b + Vector(0, 0, 34), 30, 0, 255, g, 0, 250, 5);

            if ((areas[i].flags & A_FL_2) == A_FL_2 && (areas[i].flags & A_FL_3) == A_FL_3)
               WaypointDrawBeam(pEntity, areas[i].b + Vector(0, 0, 34), areas[i].c + Vector(0, 0, 34), 30, 0, 255, g, 0, 250, 5);

            if ((areas[i].flags & A_FL_3) == A_FL_3 && (areas[i].flags & A_FL_4) == A_FL_4)
               WaypointDrawBeam(pEntity, areas[i].c + Vector(0, 0, 34), areas[i].d + Vector(0, 0, 34), 30, 0, 255, g, 0, 250, 5);

            if ((areas[i].flags & A_FL_4) == A_FL_4 && (areas[i].flags & A_FL_1) == A_FL_1)
               WaypointDrawBeam(pEntity, areas[i].d + Vector(0, 0, 34), areas[i].a + Vector(0, 0, 34), 30, 0, 255, g, 0, 250, 5);

            // ok, has this area been fully labeled?
            // display coloured lines to represent what labels
            // have been done
            if ((areas[i].flags & A_FL_1) == A_FL_1 && (areas[i].flags & A_FL_2) == A_FL_2 && (areas[i].flags & A_FL_3) == A_FL_3 && (areas[i].flags & A_FL_4) == A_FL_4) {
               //{ fp=UTIL_OpenFoxbotLog(); fprintf(fp, "i: %d\n",i); fclose(fp); }
               //{ fp=UTIL_OpenFoxbotLog(); fprintf(fp, "a: %s\n",areas[i].namea); fclose(fp); }
               // if(areas[i].namea[0]!=NULL)
               // WaypointDrawBeam(pEntity, areas[i].a + Vector(0, 0, 40), areas[i].a + Vector(0, 0, 50), 30, 0, 0,
               // 0, 255, 250, 5);
               //{ fp=UTIL_OpenFoxbotLog(); fprintf(fp, "b: %s\n",areas[i].nameb); fclose(fp); }
               // if(areas[i].nameb[0]!=NULL)
               // WaypointDrawBeam(pEntity, areas[i].b + Vector(0, 0, 40), areas[i].b + Vector(0, 0, 50), 30, 0,
               // 255, 0, 0, 250, 5);
               //{ fp=UTIL_OpenFoxbotLog(); fprintf(fp, "c: %s\n",areas[i].namec); fclose(fp); }
               // if(areas[i].namec[0]!=NULL)
               // WaypointDrawBeam(pEntity, areas[i].c + Vector(0, 0, 40), areas[i].c + Vector(0, 0, 50), 30, 0,
               // 255, 255, 0, 250, 5);
               //{ fp=UTIL_OpenFoxbotLog(); fprintf(fp, "d: %s\n",areas[i].named); fclose(fp); }
               // if(areas[i].named[0]!=NULL)
               // WaypointDrawBeam(pEntity, areas[i].d + Vector(0, 0, 40), areas[i].d + Vector(0, 0, 50), 30, 0, 0,
               // 255, 0, 250, 5);
            }
         }
         if (timr)
            a_display_time[i] = gpGlobals->time;
      }
   }
   area_on_last = g_area_def;
}

// Run Floyd's algorithm on the waypoint list to generate the least cost
// path matrix.
static void WaypointFloyds(unsigned int *path, unsigned int *to) {
   unsigned int y, z;

   for (y = 0; y < route_num_waypoints; y++) {
      for (z = 0; z < route_num_waypoints; z++) {
         to[y * route_num_waypoints + z] = z;
      }
   }

   bool changed = true;

   while (changed) {
      changed = false;
      for (unsigned int x = 0; x < route_num_waypoints; x++) {
         for (y = 0; y < route_num_waypoints; y++) {
            const unsigned int yRx = y * route_num_waypoints + x;
            for (z = 0; z < route_num_waypoints; z++) {
               // Optimization - didn't need to do that math all over
               // the place in the [], just do it once.
               const unsigned int yRz = y * route_num_waypoints + z;
               const unsigned int xRz = x * route_num_waypoints + z;

               if (path[yRx] == WAYPOINT_UNREACHABLE || path[xRz] == WAYPOINT_UNREACHABLE)
                  continue;

               unsigned int distance = path[yRx] + path[xRz];

               if (distance > WAYPOINT_MAX_DISTANCE)
                  distance = WAYPOINT_MAX_DISTANCE;

               if (distance < path[yRz] || path[yRz] == WAYPOINT_UNREACHABLE) {
                  path[yRz] = distance;
                  to[yRz] = to[yRx];
                  changed = true;
               }
            }
         }
      }
   }
}

// Set up the matrices for the waypoints and their paths, so that they are
// ready for use by the bots.
static void WaypointRouteInit() {
   if (num_waypoints < 1) // sanity check
      return;

   unsigned int index;
   bool build_matrix[4];
   int i;
   char msg[80];

   // save number of current waypoints in case waypoints get added later
   route_num_waypoints = num_waypoints;

   build_matrix[0] = true; // always build matrix 0 (non-team and team 1)
   build_matrix[1] = false;
   build_matrix[2] = false;
   build_matrix[3] = false;

   // find out how many route matrixes to create...
   for (index = 0; index < route_num_waypoints; index++) {
      if (waypoints[index].flags & W_FL_TEAM_SPECIFIC) {
         if ((waypoints[index].flags & W_FL_TEAM) == 0x01) // team 2?
            build_matrix[1] = true;

         if ((waypoints[index].flags & W_FL_TEAM) == 0x02) // team 3?
            build_matrix[2] = true;

         if ((waypoints[index].flags & W_FL_TEAM) == 0x03) // team 4?
            build_matrix[3] = true;
      }
   }

   const unsigned int array_size = route_num_waypoints * route_num_waypoints;

   for (int matrix = 0; matrix < 4; matrix++) {
      if (build_matrix[matrix]) {
         if (shortest_path[matrix] == nullptr) {
            sprintf(msg, "calculating FoXBot waypoint paths for team %d...\n", matrix + 1);
            ALERT(at_console, msg);

            shortest_path[matrix] = static_cast<unsigned int *>(malloc(sizeof(unsigned int) * array_size));

            if (shortest_path[matrix] == nullptr)
               ALERT(at_error, "FoXBot - Error allocating memory for shortest path!");

            from_to[matrix] = static_cast<unsigned int *>(malloc(sizeof(unsigned int) * array_size));

            if (from_to[matrix] == nullptr)
               ALERT(at_error, "FoXBot - Error allocating memory for from to matrix!");

            unsigned int *pShortestPath = shortest_path[matrix];
            unsigned int *pFromTo = from_to[matrix];

            for (index = 0; index < array_size; index++)
               pShortestPath[index] = WAYPOINT_UNREACHABLE;

            for (index = 0; index < route_num_waypoints; index++)
               pShortestPath[index * route_num_waypoints + index] = 0; // zero diagonal

            for (unsigned int row = 0; row < route_num_waypoints; row++) {
               if (paths[row] != nullptr) {
                  PATH *p = paths[row];

                  while (p) {
                     i = 0;

                     while (i < MAX_PATH_INDEX) {
                        if (p->index[i] != -1) {
                           index = p->index[i];

                           // check if this is NOT team specific OR
                           // matches this team
                           if (!(waypoints[index].flags & W_FL_TEAM_SPECIFIC) || (waypoints[index].flags & W_FL_TEAM) == matrix) {
                              float distance = (waypoints[row].origin - waypoints[index].origin).Length();

                              if (distance > static_cast<float>(WAYPOINT_MAX_DISTANCE))
                                 distance = static_cast<float>(WAYPOINT_MAX_DISTANCE);

                              if (distance > REACHABLE_RANGE) {
                                 sprintf(msg, "Waypoint path distance > %4.1f at from %d to %d\n", REACHABLE_RANGE, row, index);
                                 ALERT(at_console, msg);
                                 WaypointDeletePath(row, index);
                              } else {
                                 const int offset = row * route_num_waypoints + index;

                                 pShortestPath[offset] = static_cast<unsigned int>(distance);
                              }
                           }
                        }

                        i++;
                     }

                     p = p->next; // go to next node in linked list
                  }
               }
            }

            // run Floyd's Algorithm to generate the from_to matrix...
            WaypointFloyds(pShortestPath, pFromTo);

            for (unsigned int a = 0; a < route_num_waypoints; a++) {
               for (unsigned int b = 0; b < route_num_waypoints; b++)
                  if (pShortestPath[a * route_num_waypoints + b] == WAYPOINT_UNREACHABLE)
                     pFromTo[a * route_num_waypoints + b] = WAYPOINT_UNREACHABLE;
            }
            sprintf(msg, "FoXBot waypoint path calculations for team %d complete!\n", matrix + 1);
            ALERT(at_console, msg);
         }
      }
   }
   ALERT(at_console, "Loading Rocket/Conc Jump waypoints...");
   int RJIndex = 0;
   // a local array to keep track of the # of RJ points for each team
   int teamCount[4];
   for (i = 0; i < 4; i++)
      teamCount[i] = 0;

   // initialize the RJPoints to -1 so we can check for that later to
   // stop further checking
   for (i = 0; i < MAXRJWAYPOINTS; i++) {
      RJPoints[i][RJ_WP_INDEX] = -1;
      RJPoints[i][RJ_WP_TEAM] = -1;
   }
   // Go through the waypoints and look for rj/conc wps
   for (i = 0; i < num_waypoints; i++) {
      // Skip deleted waypoints.
      if (waypoints[i].flags & W_FL_DELETED)
         continue;

      // Is it a rocket jump point?
      if (waypoints[i].flags & W_FL_TFC_JUMP) {
         // Check any team specific flags
         if (waypoints[i].flags & W_FL_TEAM_SPECIFIC) {
            // Put the waypoint index, and the team # into the list.
            RJPoints[RJIndex][0] = i;
            RJPoints[RJIndex][1] = waypoints[index].flags & W_FL_TEAM;
            teamCount[RJPoints[RJIndex][1] - 1]++;
            RJIndex++;
         } else {
            // Just store the index, leave the team field at -1, means
            // no team specific
            RJPoints[RJIndex][0] = i;
            RJIndex++;
         }
         // Dont go over the limit
         if (RJIndex == MAXRJWAYPOINTS)
            break;
      }
   }
   sprintf(msg, "RJ/Conc Total: %d : Blue: %d : Red: %d : Yellow: %d : Green: %d\n", RJIndex + 1, teamCount[0], teamCount[1], teamCount[2], teamCount[3]);
   ALERT(at_console, msg);
}

// return the next waypoint index for a path from the Floyd matrix when
// going from a source waypoint index (src) to a destination waypoint
// index (dest)...
// It returns -1 if a suitable waypoint wasn't found.
int WaypointRouteFromTo(const int src, const int dest, int team) {
   // sanity check
   if (team < -1 || team > 3)
      return -1;

   // Theoretically this function could read other memory locations
   // and crash if src or dest are given dodgy values.
   if (src < 0 || dest < 0 || src >= num_waypoints || dest >= num_waypoints) {
      //	fp = UTIL_OpenFoxbotLog();
      //	fprintf(fp, "WaypointRouteFromTo() src:%d dest:%d num_waypoints:%d\n",
      //		src, dest, num_waypoints);
      //	fclose(fp);
      return -1;
   }

   // -1 means non-team play
   if (team == -1)
      team = 0;

   // if no team specific waypoints use team 0
   if (from_to[team] == nullptr)
      team = 0;

   // if no route information just return
   if (from_to[team] == nullptr)
      return -1;

   const unsigned int *pFromTo = from_to[team];

   return static_cast<int>(pFromTo[src * route_num_waypoints + dest]);
}

// return the total distance (based on the Floyd matrix) of a path from
// the source waypoint index (src) to the destination waypoint index (dest)
// Or return -1 on failure.
int WaypointDistanceFromTo(const int src, const int dest, int team) {
   if (team < -1 || team > 3)
      return -1;

   // Theoretically this function could read other memory locations
   // and crash if src or dest are given dodgy values.
   if (src < 0 || dest < 0 || src >= num_waypoints || dest >= num_waypoints) {
      //	fp = UTIL_OpenFoxbotLog();
      //	fprintf(fp, "WaypointDistanceFromTo() src:%d dest:%d num_waypoints:%d\n",
      //		src, dest, num_waypoints);
      //	fclose(fp);
      return -1;
   }

   // -1 means non-team play
   if (team == -1)
      team = 0;

   // if no team specific waypoints use team 0
   if (from_to[team] == nullptr)
      team = 0;

   // if no route information just return
   if (from_to[team] == nullptr)
      return -1;

   const unsigned int *pShortestPath = shortest_path[team];

   return static_cast<int>(pShortestPath[src * route_num_waypoints + dest]);
}

// index is the waypoint, team = bots team
// Checks to see if the waypoint has a point tag 1 - 8, and if it is
// available or not.
bool WaypointAvailable(const int index, const int team) {
   // check it's a valid waypoint first
   if (index < 0 || index >= num_waypoints)
      return false;

   // if a team was specified make sure it matches this waypoint
   if (team != -1 && waypoints[index].flags & W_FL_TEAM_SPECIFIC && (waypoints[index].flags & W_FL_TEAM) != team)
      return false;

   // a bit field of all script number waypoint flags
   // to speed the checking operation up
   static int validFlags = (S_FL_POINT1 | S_FL_POINT2 | S_FL_POINT3 | S_FL_POINT4 | S_FL_POINT5 | S_FL_POINT6 | S_FL_POINT7 | S_FL_POINT8);

   // Report true if this waypoint is scripted and currently
   // available to the indicated team
   if (team != -1 && waypoints[index].script_flags & validFlags) {
      bool waypoint_is_scripted = false;
      if (waypoints[index].script_flags & S_FL_POINT1) {
         waypoint_is_scripted = true;
         switch (team) {
         case 0:
            if (blue_av[0] == true)
               return true;
            break;
         case 1:
            if (red_av[0] == true)
               return true;
            break;
         case 2:
            if (yellow_av[0] == true)
               return true;
            break;
         case 3:
            if (green_av[0] == true)
               return true;
            break;
         }
      }
      if (waypoints[index].script_flags & S_FL_POINT2) {
         waypoint_is_scripted = true;
         switch (team) {
         case 0:
            if (blue_av[1] == true)
               return true;
            break;
         case 1:
            if (red_av[1] == true)
               return true;
            break;
         case 2:
            if (yellow_av[1] == true)
               return true;
            break;
         case 3:
            if (green_av[1] == true)
               return true;
            break;
         }
      }
      if (waypoints[index].script_flags & S_FL_POINT3) {
         waypoint_is_scripted = true;
         switch (team) {
         case 0:
            if (blue_av[2] == true)
               return true;
            break;
         case 1:
            if (red_av[2] == true)
               return true;
            break;
         case 2:
            if (yellow_av[2] == true)
               return true;
            break;
         case 3:
            if (green_av[2] == true)
               return true;
            break;
         }
      }
      if (waypoints[index].script_flags & S_FL_POINT4) {
         waypoint_is_scripted = true;
         switch (team) {
         case 0:
            if (blue_av[3] == true)
               return true;
            break;
         case 1:
            if (red_av[3] == true)
               return true;
            break;
         case 2:
            if (yellow_av[3] == true)
               return true;
            break;
         case 3:
            if (green_av[3] == true)
               return true;
            break;
         }
      }
      if (waypoints[index].script_flags & S_FL_POINT5) {
         waypoint_is_scripted = true;
         switch (team) {
         case 0:
            if (blue_av[4] == true)
               return true;
            break;
         case 1:
            if (red_av[4] == true)
               return true;
            break;
         case 2:
            if (yellow_av[4] == true)
               return true;
            break;
         case 3:
            if (green_av[4] == true)
               return true;
            break;
         }
      }
      if (waypoints[index].script_flags & S_FL_POINT6) {
         waypoint_is_scripted = true;
         switch (team) {
         case 0:
            if (blue_av[5] == true)
               return true;
            break;
         case 1:
            if (red_av[5] == true)
               return true;
            break;
         case 2:
            if (yellow_av[5] == true)
               return true;
            break;
         case 3:
            if (green_av[5] == true)
               return true;
            break;
         }
      }
      if (waypoints[index].script_flags & S_FL_POINT7) {
         waypoint_is_scripted = true;
         switch (team) {
         case 0:
            if (blue_av[6] == true)
               return true;
            break;
         case 1:
            if (red_av[6] == true)
               return true;
            break;
         case 2:
            if (yellow_av[6] == true)
               return true;
            break;
         case 3:
            if (green_av[6] == true)
               return true;
            break;
         }
      }
      if (waypoints[index].script_flags & S_FL_POINT8) {
         waypoint_is_scripted = true;
         switch (team) {
         case 0:
            if (blue_av[7] == true)
               return true;
            break;
         case 1:
            if (red_av[7] == true)
               return true;
            break;
         case 2:
            if (yellow_av[7] == true)
               return true;
            break;
         case 3:
            if (green_av[7] == true)
               return true;
            break;
         }
      }

      // this waypoint must be scripted but currently unavailable
      // so return false (waypoint is unavailable)
      if (waypoint_is_scripted)
         return false;
   }

   // if the code ran this far the waypoint is suitable for
   // use by this bot
   return true;
}

void WaypointRunOneWay(edict_t *pEntity) {
   const int temp = WaypointFindNearest_E(pEntity, 50.0, -1);

   if (temp != -1) {
      if (wpt1 == -1) {
         // play "cancelled" sound...
         EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_moveselect.wav", 1.0, ATTN_NORM, 0, 100);
         wpt1 = temp;
         return;
      } else {
         wpt2 = wpt1;
         wpt1 = temp;
         if (wpt1 != -1 && wpt2 != -1 && wpt1 != wpt2) {
            // play "error" sound...
            EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_select.wav", 1.0, ATTN_NORM, 0, 100);

            WaypointAddPath(wpt2, wpt1);
         }
         return;
      }
   }
   return;
}

void WaypointRunTwoWay(edict_t *pEntity) {
   const int temp = WaypointFindNearest_E(pEntity, 50.0, -1);

   if (temp != -1) {
      if (wpt1 == -1) {
         // play "cancelled" sound...
         EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_moveselect.wav", 1.0, ATTN_NORM, 0, 100);
         wpt1 = temp;
         return;
      } else {
         wpt2 = wpt1;
         wpt1 = temp;
         if (wpt1 != -1 && wpt2 != -1 && wpt1 != wpt2) {
            // play "error" sound...
            EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "common/wpn_select.wav", 1.0, ATTN_NORM, 0, 100);

            WaypointAddPath(wpt1, wpt2);
            WaypointAddPath(wpt2, wpt1);
         }
         return;
      }
   }
   return;
}

void WaypointAutoBuild(edict_t *pEntity) {
   // edict_t *pent = NULL;
   // float radius = 40;
   //	char item_name[64];
   // clear all wpts
   WaypointInit();

   for (int x = -4096 + 32; x <= 4096 - 32; x = x + 32) {
      /*{ fp=UTIL_OpenFoxbotLog();
                      fprintf(fp,"%d %d\n\n",x,num_waypoints); fclose(fp); }*/

      for (int y = -4096 + 32; y <= 4096 - 32; y = y + 32) {
         for (int z = -4096 + (32 + 32); z <= 4096 - 32; z = z + 32) {
            /*{ fp=UTIL_OpenFoxbotLog();
                            fprintf(fp,"-%d %d\n",y,num_waypoints); fclose(fp); }*/

            TraceResult tr;
            // Vector(x,y,(-4096+32))
            // UTIL_TraceLine(Vector(x,y,z),Vector(x,y,z-64)
            //,ignore_monsters,ignore_glass,pEntity,&tr);
            TRACE_HULL(Vector(x, y, z), Vector(x, y, z - 32), ignore_monsters, head_hull, pEntity, &tr);

            /*{ fp=UTIL_OpenFoxbotLog();
                            fprintf(fp,"%f\n",tr.flFraction); fclose(fp); }*/

            // while(tr.flFraction<1)
            if (tr.flFraction < 1) {
               if (num_waypoints >= MAX_WAYPOINTS)
                  return;
               // make sure player can at least duck in this space!
               // TraceResult tr2;
               // UTIL_TraceLine(tr.vecEndPos,tr.vecEndPos+Vector(0,0,32)
               //,ignore_monsters,ignore_glass,pEntity,&tr2);
               // TRACE_HULL(tr.vecEndPos,tr.vecEndPos+Vector(0,0,32)
               //,ignore_monsters,head_hull,pEntity,&tr2);
               // if(tr2.flFraction>=1)
               {
                  int index = 0;

                  // find the next available slot for the new waypoint...
                  while (index < num_waypoints) {
                     if (waypoints[index].flags & W_FL_DELETED)
                        break;

                     index++;
                  }

                  waypoints[index].flags = 0;

                  // store the origin (location) of this waypoint
                  // (use entity origin)
                  waypoints[index].origin = tr.vecEndPos + Vector(0, 0, 16);

                  // store the last used waypoint for the auto waypoint code...
                  // last_waypoint = tr.vecEndPos;

                  // set the time that this waypoint was originally
                  // displayed...
                  wp_display_time[index] = gpGlobals->time;

                  // Vector start = tr.vecEndPos - Vector(0, 0, 34);
                  // Vector end = start + Vector(0, 0, 68);

                  //********************************************************
                  // look for lift, ammo, flag, health, armor, etc.
                  //********************************************************

                  /*while((pent = FIND_ENTITY_IN_SPHERE( pent, tr.vecEndPos, radius )) != NULL
                                  && (!FNullEnt(pent)))
                                  {
                                  strcpy(item_name, STRING(pent->v.classname));

                                  if(strcmp("item_healthkit", item_name) == 0)
                                  {
                                  //ClientPrint(pEntity, HUD_PRINTCONSOLE, "found a healthkit!\n");
                                  waypoints[index].flags |= W_FL_HEALTH;
                                  }

                                  if(strncmp("item_armor", item_name, 10) == 0)
                                  {
                                  //ClientPrint(pEntity, HUD_PRINTCONSOLE, "found some armor!\n");
                                  waypoints[index].flags |= W_FL_ARMOR;
                                  }

                                  // *************
                                  // LOOK FOR AMMO
                                  // *************
                                  }*/

                  // draw a blue waypoint
                  /*WaypointDrawBeam(pEntity, start, end,
                                  30, 0, 0, 0, 255, 250, 5);*/

                  // increment total number of waypoints if adding
                  // at end of array...
                  if (index == num_waypoints)
                     num_waypoints++;

                  // delete stuff just incase.
                  WaypointDeletePath(index);
                  // delete from index to other..cus his delete function dont!

                  PATH *p = paths[index];
                  // int count = 0;
                  // search linked list for del_index...
                  while (p != nullptr) {
                     int i = 0;
                     while (i < MAX_PATH_INDEX) {
                        p->index[i] = -1; // unassign this path
                        i++;
                     }
                     p = p->next; // go to next node in linked list

                     /*#ifdef _DEBUG
                                                                             count++;
                                                                             if(count > 100)
                                                                                     WaypointDebug();
                     #endif*/
                  }

                  /*if(g_path_connect)
                                  {
                                  // calculate all the paths to this new waypoint
                                  for(int i=0; i < num_waypoints; i++)
                                  {
                                  if(i == index)
                                  continue;  // skip the waypoint that was just added

                                  if(waypoints[i].flags & W_FL_AIMING)
                                  continue;  // skip any aiming waypoints

                                  if((waypoints[i].flags & W_FL_DELETED)==W_FL_DELETED)
                                  continue; //skip deleted wpts!!

                                  // check if the waypoint is reachable from the new one (one-way)
                                  if( WaypointReachable(waypoints[index].origin, waypoints[i].origin, pEntity) )
                                  {
                                  WaypointAddPath(index, i);
                                  }

                                  // check if the new one is reachable from the waypoint (other way)
                                  if( WaypointReachable(waypoints[i].origin, waypoints[index].origin, pEntity) )
                                  {
                                  WaypointAddPath(i, index);
                                  }
                                  }
                                  }*/
               }
               // now do the next trace
               /*				  if(tr.vecEndPos.z-32>(-4096+32))
                               {
                               UTIL_TraceLine(tr.vecEndPos-Vector(0,0,32),Vector(x,y,(-4096+32))
                               ,ignore_monsters,ignore_glass,pEntity,&tr);
                               }
                               else
                               tr.flFraction=1;*/
            }
         }
      }
   }
}

// area def stuff..

void AreaDefCreate(edict_t *pEntity) {
   if (num_areas >= MAX_WAYPOINTS)
      return;

   int index = 0;

   // find the next available slot for the new waypoint...
   while (index < num_areas) {
      if ((areas[index].flags & W_FL_DELETED) == W_FL_DELETED)
         break;

      // stop here if we havn't finished filling an area with points
      if (!((areas[index].flags & A_FL_1) == A_FL_1 && (areas[index].flags & A_FL_2) == A_FL_2 && (areas[index].flags & A_FL_3) == A_FL_3 && (areas[index].flags & A_FL_4) == A_FL_4))
         break;

      index++;
   }

   if (index == num_areas)
      num_areas++;
   // areas[index].flags = 0;

   if (!((areas[index].flags & A_FL_1) == A_FL_1)) {
      areas[index].a = pEntity->v.origin;
      areas[index].flags |= A_FL_1;
      // set the time that this waypoint was originally displayed...
      a_display_time[index] = gpGlobals->time;

      const Vector start = pEntity->v.origin - Vector(0, 0, 34);
      const Vector end = start + Vector(0, 0, 68);

      // draw a blue waypoint
      WaypointDrawBeam(pEntity, start, end, 30, 0, 255, 0, 0, 250, 5);

      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/xbow_hit1.wav", 1.0, ATTN_NORM, 0, 100);
      return;
   }

   if (!((areas[index].flags & A_FL_2) == A_FL_2)) {
      areas[index].b = pEntity->v.origin;
      areas[index].flags |= A_FL_2;
      // set the time that this waypoint was originally displayed...
      a_display_time[index] = gpGlobals->time;

      const Vector start = pEntity->v.origin - Vector(0, 0, 34);
      const Vector end = start + Vector(0, 0, 68);

      // draw a blue waypoint
      WaypointDrawBeam(pEntity, start, end, 30, 0, 255, 0, 0, 250, 5);

      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/xbow_hit1.wav", 1.0, ATTN_NORM, 0, 100);
      return;
   }

   if (!((areas[index].flags & A_FL_3) == A_FL_3)) {
      areas[index].c = pEntity->v.origin;
      areas[index].flags |= A_FL_3;
      // set the time that this waypoint was originally displayed...
      a_display_time[index] = gpGlobals->time;

      const Vector start = pEntity->v.origin - Vector(0, 0, 34);
      const Vector end = start + Vector(0, 0, 68);

      // draw a blue waypoint
      WaypointDrawBeam(pEntity, start, end, 30, 0, 255, 0, 0, 250, 5);

      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/xbow_hit1.wav", 1.0, ATTN_NORM, 0, 100);
      return;
   }

   if (!((areas[index].flags & A_FL_4) == A_FL_4)) {
      areas[index].d = pEntity->v.origin;
      areas[index].flags |= A_FL_4;
      // set the time that this waypoint was originally displayed...
      a_display_time[index] = gpGlobals->time;

      const Vector start = pEntity->v.origin - Vector(0, 0, 34);
      const Vector end = start + Vector(0, 0, 68);

      // draw a blue waypoint
      WaypointDrawBeam(pEntity, start, end, 30, 0, 255, 0, 0, 250, 5);

      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/xbow_hit1.wav", 1.0, ATTN_NORM, 0, 100);
      return;
   }
}

int AreaDefPointFindNearest(const edict_t *pEntity, const float range, const int flags) {
   if (num_areas < 1)
      return -1;

   int min_index = -1;
   float min_distance = 9999.0;
   TraceResult tr;
   Vector o;

   // find the nearest area point (not whole area)...

   for (int i = 0; i < num_areas; i++) {
      if (areas[i].flags & W_FL_DELETED)
         continue; // skip any deleted area points

      float distance = 9999;

      if (flags == A_FL_1) {
         distance = (areas[i].a - pEntity->v.origin).Length();
         o = areas[i].a;
      }
      if (flags == A_FL_2) {
         distance = (areas[i].b - pEntity->v.origin).Length();
         o = areas[i].b;
      }
      if (flags == A_FL_3) {
         distance = (areas[i].c - pEntity->v.origin).Length();
         o = areas[i].c;
      }
      if (flags == A_FL_4) {
         distance = (areas[i].d - pEntity->v.origin).Length();
         o = areas[i].d;
      }

      if (distance == 9999)
         continue;

      if (distance < min_distance && distance < range) {
         // if waypoint is visible from current position
         // (even behind head)...
         UTIL_TraceLine(pEntity->v.origin + pEntity->v.view_ofs, o, ignore_monsters, pEntity->v.pContainingEntity, &tr);

         if (tr.flFraction >= 1.0) {
            min_index = i;
            min_distance = distance;
         }
      }
   }

   return min_index;
}

void AreaDefDelete(edict_t *pEntity) {
   if (num_areas < 1)
      return;

   // int count = 0;

   int index = AreaDefPointFindNearest(pEntity, 50.0, A_FL_1);
   if (index != -1) {
      areas[index].flags &= ~A_FL_1;
      areas[index].a = Vector(0, 0, 0);

      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/mine_activate.wav", 1.0, ATTN_NORM, 0, 100);
      return;
   }
   index = AreaDefPointFindNearest(pEntity, 50.0, A_FL_2);
   if (index != -1) {
      areas[index].flags &= ~A_FL_2;
      areas[index].b = Vector(0, 0, 0);

      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/mine_activate.wav", 1.0, ATTN_NORM, 0, 100);
      return;
   }
   index = AreaDefPointFindNearest(pEntity, 50.0, A_FL_3);
   if (index != -1) {
      areas[index].flags &= ~A_FL_3;
      areas[index].c = Vector(0, 0, 0);

      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/mine_activate.wav", 1.0, ATTN_NORM, 0, 100);
      return;
   }
   index = AreaDefPointFindNearest(pEntity, 50.0, A_FL_4);
   if (index != -1) {
      areas[index].flags &= ~A_FL_4;
      areas[index].d = Vector(0, 0, 0);

      EMIT_SOUND_DYN2(pEntity, CHAN_WEAPON, "weapons/mine_activate.wav", 1.0, ATTN_NORM, 0, 100);
      return;
   }
}

void AreaDefSave() {
   char filename[256];
   char mapname[64];
   AREA_HDR header;

   strcpy(header.filetype, "FoXBot");

   header.area_file_version = AREA_VERSION;

   header.number_of_areas = num_areas;

   memset(header.mapname, 0, sizeof header.mapname);
   strncpy(header.mapname, STRING(gpGlobals->mapname), 31);
   header.mapname[31] = 0;

   strcpy(mapname, STRING(gpGlobals->mapname));
   strcat(mapname, ".far");

   UTIL_BuildFileName(filename, 255, "areas", mapname);

   FILE *bfp = fopen(filename, "wb");

   // write the waypoint header to the file...
   fwrite(&header, sizeof header, 1, bfp);

   // write the waypoint data to the file...
   for (int index = 0; index < num_areas; index++) {
      fwrite(&areas[index], sizeof areas[0], 1, bfp);
   }

   fclose(bfp);
}

bool AreaDefLoad(edict_t *pEntity) {
   char mapname[64];
   char filename[256];
   AREA_HDR header;

   // return false; //test to see if having areas loaded is
   // fucking up the server (mem crashes)

   strcpy(mapname, STRING(gpGlobals->mapname));
   strcat(mapname, ".far");

   UTIL_BuildFileName(filename, 255, "areas", mapname);
   FILE *bfp = fopen(filename, "rb");

   if (bfp != nullptr) {
      char msg[256];
      if (IS_DEDICATED_SERVER())
         printf("loading area file: %s\n", filename);
      fread(&header, sizeof header, 1, bfp);

      header.filetype[7] = 0;
      if (strcmp(header.filetype, "FoXBot") == 0) {
         if (header.area_file_version != AREA_VERSION) {
            if (pEntity)
               ClientPrint(pEntity, HUD_PRINTNOTIFY, "Incompatible FoXBot area file version\nAreas not loaded\n");

            fclose(bfp);
            return false;
         }

         header.mapname[31] = 0;

         if (strcasecmp(header.mapname, STRING(gpGlobals->mapname)) == 0) {
            // works for areas aswell :)
            // WaypointInit();  // remove any existing waypoints
            // grr, removes loaded waypoints! doh
            num_areas = 0;

            int i;
            for (i = 0; i < MAX_WAYPOINTS; i++) {
               a_display_time[i] = 0.0;
               areas[i].flags = 0;
               areas[i].a = Vector(0, 0, 0);
               areas[i].b = Vector(0, 0, 0);
               areas[i].c = Vector(0, 0, 0);
               areas[i].d = Vector(0, 0, 0);
               areas[i].namea[0] = '\0';
               areas[i].nameb[0] = '\0';
               areas[i].namec[0] = '\0';
               areas[i].named[0] = '\0';
            }

            if (pEntity)
               ClientPrint(pEntity, HUD_PRINTNOTIFY, "Loading FoXBot area file\n");

            for (i = 0; i < header.number_of_areas; i++) {
               fread(&areas[i], sizeof areas[0], 1, bfp);
               num_areas++;
            }
         } else {
            if (pEntity) {
               sprintf(msg, "%s FoXBot areas aren't for this map\n", filename);

               ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
            }

            fclose(bfp);
            return false;
         }
      } else {
         if (pEntity) {
            sprintf(msg, "%s is not a FoXBot area file\n", filename);
            ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
         }

         fclose(bfp);
         return false;
      }

      fclose(bfp);
   }

   return true;
}

void AreaDefPrintInfo(edict_t *pEntity) {
   char msg[1024];
   // int flags;

   const int i = AreaInsideClosest(pEntity);
   if (i != -1) {
      snprintf(msg, 1020, "Area %d of %d total\n", i, num_areas);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
      snprintf(msg, 1020, "Name1 = %s\n", areas[i].namea);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
      snprintf(msg, 1020, "Name2 = %s\n", areas[i].nameb);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
      snprintf(msg, 1020, "Name3 = %s\n", areas[i].namec);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
      snprintf(msg, 1020, "Name4 = %s\n", areas[i].named);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
   }

   int index = AreaDefPointFindNearest(pEntity, 50.0, A_FL_1);

   if (index != -1) {
      sprintf(msg, "Area %d of %d total\n", index, num_areas);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "Area corner 1\n");
      return;
   }

   index = AreaDefPointFindNearest(pEntity, 50.0, A_FL_2);

   if (index != -1) {
      sprintf(msg, "Area %d of %d total\n", index, num_areas);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "Area corner 2\n");
      return;
   }

   index = AreaDefPointFindNearest(pEntity, 50.0, A_FL_3);

   if (index != -1) {
      sprintf(msg, "Area %d of %d total\n", index, num_areas);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "Area corner 3\n");
      return;
   }

   index = AreaDefPointFindNearest(pEntity, 50.0, A_FL_4);

   if (index != -1) {
      sprintf(msg, "Area %d of %d total\n", index, num_areas);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, msg);
      ClientPrint(pEntity, HUD_PRINTNOTIFY, "Area corner 4\n");
      return;
   }
}

bool AreaInside(const edict_t *pEntity, const int i) {
   bool inside = false;
   if ((areas[i].flags & A_FL_1) == A_FL_1 && (areas[i].flags & A_FL_2) == A_FL_2 && (areas[i].flags & A_FL_3) == A_FL_3 && (areas[i].flags & A_FL_4) == A_FL_4) {
      const float x = pEntity->v.origin.x;
      const float y = pEntity->v.origin.y;

      float lx = areas[i].a.x;
      if (areas[i].b.x < lx)
         lx = areas[i].b.x;
      if (areas[i].c.x < lx)
         lx = areas[i].c.x;
      if (areas[i].d.x < lx)
         lx = areas[i].d.x;
      float ly = areas[i].a.y;
      if (areas[i].b.y < ly)
         ly = areas[i].b.y;
      if (areas[i].c.y < ly)
         ly = areas[i].c.y;
      if (areas[i].d.y < ly)
         ly = areas[i].d.y;
      float hx = areas[i].a.x;
      if (areas[i].b.x > hx)
         hx = areas[i].b.x;
      if (areas[i].c.x > hx)
         hx = areas[i].c.x;
      if (areas[i].d.x > hx)
         hx = areas[i].d.x;
      float hy = areas[i].a.y;
      if (areas[i].b.y > hy)
         hy = areas[i].b.y;
      if (areas[i].c.y > hy)
         hy = areas[i].c.y;
      if (areas[i].d.y > hy)
         hy = areas[i].d.y;

      // make sure there's no rounding errors (I think vect = float..)
      lx = lx - 1;
      hx = hx + 1;
      ly = ly - 1;
      hy = hy + 1;

      // now the in-bounds check..
      if (x >= lx && x <= hx && y >= ly && y <= hy) {
         if ((areas[i].a.y < y && areas[i].b.y >= y) || (areas[i].b.y < y && areas[i].a.y >= y)) {
            if (areas[i].a.x + (y - areas[i].a.y) / (areas[i].b.y - areas[i].a.y) * (areas[i].b.x - areas[i].a.x) < x)
               inside = !inside;
         }
         if ((areas[i].b.y < y && areas[i].c.y >= y) || (areas[i].c.y < y && areas[i].b.y >= y)) {
            if (areas[i].b.x + (y - areas[i].b.y) / (areas[i].c.y - areas[i].b.y) * (areas[i].c.x - areas[i].b.x) < x)
               inside = !inside;
         }
         if ((areas[i].c.y < y && areas[i].d.y >= y) || (areas[i].d.y < y && areas[i].c.y >= y)) {
            if (areas[i].c.x + (y - areas[i].c.y) / (areas[i].d.y - areas[i].c.y) * (areas[i].d.x - areas[i].c.x) < x)
               inside = !inside;
         }
         if ((areas[i].d.y < y && areas[i].a.y >= y) || (areas[i].a.y < y && areas[i].d.y >= y)) {
            if (areas[i].d.x + (y - areas[i].d.y) / (areas[i].a.y - areas[i].d.y) * (areas[i].a.x - areas[i].d.x) < x)
               inside = !inside;
         }
      }
   }
   return inside;
}

// rix note - I think this function returns the index of the area
// around the specified entity, or -1 on failure at finding one.
int AreaInsideClosest(const edict_t *pEntity) {
   int index = -1;
   float distance = 9999.0f;

   for (int i = 0; i < num_areas; i++) {
      if (AreaInside(pEntity, i)) {
         float lz = areas[i].a.z;
         if (areas[i].b.z < lz)
            lz = areas[i].b.z;
         if (areas[i].c.z < lz)
            lz = areas[i].c.z;
         if (areas[i].d.z < lz)
            lz = areas[i].d.z;

         float hz = areas[i].a.z;

         if (areas[i].b.z > hz)
            hz = areas[i].b.z;
         if (areas[i].c.z > hz)
            hz = areas[i].c.z;
         if (areas[i].d.z > hz)
            hz = areas[i].d.z;

         // we want the mid point between hz and lz.. that will be
         // our distance..
         // nearly forgot, the distance revolves around the player!
         const float a = fabsf((hz - lz) / 2 + lz - pEntity->v.origin.z);
         if (a < distance) {
            distance = a;
            index = i;
         }
      }
   }
   return index;
}

void AreaAutoBuild1() {
   int h, i, j, k, index;
   int lc, rc, r, l;
   bool ru, lu, rd, ld;
   int lr, ll;
   for (i = 0; i <= num_waypoints; i++) {
      if (!(waypoints[i].flags & W_FL_DELETED)) {
         if (num_areas >= MAX_WAYPOINTS)
            return;

         index = 0;

         // find the next available slot for the new waypoint...
         while (index < num_areas) {
            if ((areas[index].flags & W_FL_DELETED) == W_FL_DELETED) {
               break;
            }
            // stop here if we havn't finished filling an area wif points
            if (!((areas[index].flags & A_FL_1) == A_FL_1 && (areas[index].flags & A_FL_2) == A_FL_2 && (areas[index].flags & A_FL_3) == A_FL_3 && (areas[index].flags & A_FL_4) == A_FL_4)) {
               break;
            }
            index++;
         }

         if (index == num_areas)
            num_areas++;
         areas[index].flags = A_FL_1 + A_FL_2 + A_FL_3 + A_FL_4;
         areas[index].a = waypoints[i].origin + Vector(-16, -16, 0);
         areas[index].b = waypoints[i].origin + Vector(-16, 16, 0);
         areas[index].c = waypoints[i].origin + Vector(16, 16, 0);
         areas[index].d = waypoints[i].origin + Vector(16, -16, 0);

         waypoints[i].flags = W_FL_DELETED;
         // initial area done
         // now expand it
         // look for its neighbours
         bool expanded = true;

         lc = 0;
         rc = 0;

         //	r = 0;
         //	l = 0;
         k = 0;
         ru = false;
         lu = false;
         rd = false;
         ld = false;
         ll = 0;
         lr = 0;
         while (k <= num_waypoints) {
            if (waypoints[i].origin.y == waypoints[k].origin.y && waypoints[i].origin.z == waypoints[k].origin.z && i != k) {
               if (waypoints[i].origin.x - 32 * (lc + 1) == waypoints[k].origin.x) {
                  k = -1;
                  lc++;
               }
               if (waypoints[i].origin.x + 32 * (rc + 1) == waypoints[k].origin.x) {
                  k = -1;
                  rc++;
               }
            } else if (waypoints[i].origin.y == waypoints[k].origin.y && waypoints[i].origin.z - 16 <= waypoints[k].origin.z && waypoints[i].origin.z + 16 >= waypoints[k].origin.z && !lu && !ld && i != k) {
               if (waypoints[i].origin.x - 32 * (lc + 1) == waypoints[k].origin.x) {
                  ll = k;
                  k = -1;
                  lc++;
                  if (waypoints[i].origin.z <= waypoints[k].origin.z)
                     ld = true;
                  else
                     lu = true;
               }
            } else if (waypoints[i].origin.y == waypoints[k].origin.y && waypoints[i].origin.z - 16 <= waypoints[k].origin.z && waypoints[i].origin.z + 16 >= waypoints[k].origin.z && !ru && !rd && i != k) {
               if (waypoints[i].origin.x + 32 * (rc + 1) == waypoints[k].origin.x) {
                  lr = k;
                  k = -1;
                  rc++;
                  if (waypoints[i].origin.z <= waypoints[k].origin.z)
                     rd = true;
                  else
                     ru = true;
               }
            } else if ((waypoints[i].origin.y == waypoints[k].origin.y && (waypoints[ll].origin.z - 16 <= waypoints[k].origin.z && waypoints[ll].origin.z + 16 >= waypoints[k].origin.z)) ||
                       (waypoints[lr].origin.z - 16 <= waypoints[k].origin.z && waypoints[lr].origin.z + 16 >= waypoints[k].origin.z && (ru || rd) && (ld || lu) && i != k)) {
               if (waypoints[i].origin.x - 32 * (lc + 1) == waypoints[k].origin.x) {
                  if (waypoints[i].origin.z <= waypoints[k].origin.z && ld) {
                     ll = k;
                     k = -1;
                     lc++;
                  } else if (lu) {
                     ll = k;
                     k = -1;
                     lc++;
                  }
               }
               if (waypoints[i].origin.x + 32 * (rc + 1) == waypoints[k].origin.x) {
                  if (waypoints[i].origin.z <= waypoints[k].origin.z && rd) {
                     lr = k;
                     k = -1;
                     rc++;
                  } else if (ru) {
                     lr = k;
                     k = -1;
                     rc++;
                  }
               }
            }
            k++;
         }
         while (expanded) {
            expanded = false;
            for (j = 0; j <= num_waypoints; j++) {
               if (!(waypoints[j].flags & W_FL_DELETED)) {
                  // expand via y
                  // and no slopeing in z
                  if (waypoints[i].origin.x == waypoints[j].origin.x && waypoints[i].origin.z == waypoints[j].origin.z && i != j) {
                     // expand one way..
                     if (waypoints[j].origin.y - 16 == areas[index].b.y) {
                        r = 0;
                        l = 0;
                        k = 0;
                        ru = false;
                        lu = false;
                        rd = false;
                        ld = false;
                        ll = 0;
                        lr = 0;
                        while (k <= num_waypoints) {
                           if (waypoints[j].origin.y == waypoints[k].origin.y && waypoints[j].origin.z == waypoints[k].origin.z && j != k) {
                              if (waypoints[j].origin.x - 32 * (l + 1) == waypoints[k].origin.x) {
                                 k = -1;
                                 l++;
                              }
                              if (waypoints[j].origin.x + 32 * (r + 1) == waypoints[k].origin.x) {
                                 k = -1;
                                 r++;
                              }
                           } else if (waypoints[j].origin.y == waypoints[k].origin.y && waypoints[j].origin.z - 16 <= waypoints[k].origin.z && waypoints[j].origin.z + 16 >= waypoints[k].origin.z && !lu && !ld && j != k) {
                              if (waypoints[j].origin.x - 32 * (l + 1) == waypoints[k].origin.x) {
                                 ll = k;
                                 k = -1;
                                 l++;
                                 if (waypoints[j].origin.z <= waypoints[k].origin.z)
                                    ld = true;
                                 else
                                    lu = true;
                              }
                           } else if (waypoints[j].origin.y == waypoints[k].origin.y && waypoints[j].origin.z - 16 <= waypoints[k].origin.z && waypoints[j].origin.z + 16 >= waypoints[k].origin.z && !ru && !rd && j != k) {
                              if (waypoints[j].origin.x + 32 * (r + 1) == waypoints[k].origin.x) {
                                 lr = k;
                                 k = -1;
                                 r++;
                                 if (waypoints[j].origin.z <= waypoints[k].origin.z)
                                    rd = true;
                                 else
                                    ru = true;
                              }
                           } else if ((waypoints[j].origin.y == waypoints[k].origin.y && (waypoints[ll].origin.z - 16 <= waypoints[k].origin.z && waypoints[ll].origin.z + 16 >= waypoints[k].origin.z)) ||
                                      (waypoints[lr].origin.z - 16 <= waypoints[k].origin.z && waypoints[lr].origin.z + 16 >= waypoints[k].origin.z && (ru || rd) && (ld || lu) && j != k)) {
                              if (waypoints[j].origin.x - 32 * (l + 1) == waypoints[k].origin.x) {
                                 if (waypoints[j].origin.z <= waypoints[k].origin.z && ld) {
                                    ll = k;
                                    k = -1;
                                    l++;
                                 } else if (lu) {
                                    ll = k;
                                    k = -1;
                                    l++;
                                 }
                              }
                              if (waypoints[j].origin.x + 32 * (r + 1) == waypoints[k].origin.x) {
                                 if (waypoints[j].origin.z <= waypoints[k].origin.z && rd) {
                                    lr = k;
                                    k = -1;
                                    r++;
                                 } else if (ru) {
                                    lr = k;
                                    k = -1;
                                    r++;
                                 }
                              }
                           }
                           k++;
                        }
                        if (rc == r && lc == l) {
                           areas[index].b.y = waypoints[j].origin.y + 16;
                           areas[index].c.y = waypoints[j].origin.y + 16;
                           waypoints[j].flags = W_FL_DELETED;
                           expanded = true;
                        }
                     }
                     // expand the other way..
                     else if (waypoints[j].origin.y + 16 == areas[index].a.y) {
                        r = 0;
                        l = 0;
                        k = 0;
                        ru = false;
                        lu = false;
                        rd = false;
                        ld = false;
                        ll = 0;
                        lr = 0;
                        while (k <= num_waypoints) {
                           if (waypoints[j].origin.y == waypoints[k].origin.y && waypoints[j].origin.z == waypoints[k].origin.z && j != k) {
                              if (waypoints[j].origin.x - 32 * (l + 1) == waypoints[k].origin.x) {
                                 k = -1;
                                 l++;
                              }
                              if (waypoints[j].origin.x + 32 * (r + 1) == waypoints[k].origin.x) {
                                 k = -1;
                                 r++;
                              }
                           } else if (waypoints[j].origin.y == waypoints[k].origin.y && waypoints[j].origin.z - 16 <= waypoints[k].origin.z && waypoints[j].origin.z + 16 >= waypoints[k].origin.z && !lu && !ld && j != k) {
                              if (waypoints[j].origin.x - 32 * (l + 1) == waypoints[k].origin.x) {
                                 ll = k;
                                 k = -1;
                                 l++;
                                 if (waypoints[j].origin.z <= waypoints[k].origin.z)
                                    ld = true;
                                 else
                                    lu = true;
                              }
                           } else if (waypoints[j].origin.y == waypoints[k].origin.y && waypoints[j].origin.z - 16 <= waypoints[k].origin.z && waypoints[j].origin.z + 16 >= waypoints[k].origin.z && !ru && !rd && j != k) {
                              if (waypoints[j].origin.x + 32 * (r + 1) == waypoints[k].origin.x) {
                                 lr = k;
                                 k = -1;
                                 r++;
                                 if (waypoints[j].origin.z <= waypoints[k].origin.z)
                                    rd = true;
                                 else
                                    ru = true;
                              }
                           } else if ((waypoints[j].origin.y == waypoints[k].origin.y && (waypoints[ll].origin.z - 16 <= waypoints[k].origin.z && waypoints[ll].origin.z + 16 >= waypoints[k].origin.z)) ||
                                      (waypoints[lr].origin.z - 16 <= waypoints[k].origin.z && waypoints[lr].origin.z + 16 >= waypoints[k].origin.z && (ru || rd) && (ld || lu) && j != k)) {
                              if (waypoints[j].origin.x - 32 * (l + 1) == waypoints[k].origin.x) {
                                 if (waypoints[j].origin.z <= waypoints[k].origin.z && ld) {
                                    ll = k;
                                    k = -1;
                                    l++;
                                 } else if (lu) {
                                    ll = k;
                                    k = -1;
                                    l++;
                                 }
                              }
                              if (waypoints[j].origin.x + 32 * (r + 1) == waypoints[k].origin.x) {
                                 if (waypoints[j].origin.z <= waypoints[k].origin.z && rd) {
                                    lr = k;
                                    k = -1;
                                    r++;
                                 } else if (ru) {
                                    lr = k;
                                    k = -1;
                                    r++;
                                 }
                              }
                           }
                           k++;
                        }
                        if (rc == r && lc == l) {
                           areas[index].a.y = waypoints[j].origin.y - 16;
                           areas[index].d.y = waypoints[j].origin.y - 16;
                           waypoints[j].flags = W_FL_DELETED;
                           expanded = true;
                        }
                     }
                  }
               }
            }
         }
      }
   }
   // now all areas have been created (from all the wpts)
   // their will be lots of parallel areas that can be merged...
   for (i = 0; i <= num_areas; i++) {
      if (!(areas[i].flags & W_FL_DELETED)) {
         if ((areas[i].flags & A_FL_1) == A_FL_1 && (areas[i].flags & A_FL_2) == A_FL_2 && (areas[i].flags & A_FL_3) == A_FL_3 && (areas[i].flags & A_FL_4) == A_FL_4) {
            lc = 0;
            rc = 0;

            //	r = 0;
            //	l = 0;
            k = 0;
            h = 0;
            ru = false;
            lu = false;
            rd = false;
            ld = false;
            ll = 0;
            lr = 0;
            while (k <= num_waypoints) {
               if (waypoints[k].origin == areas[i].a + Vector(16, 16, 0)) {
                  h = k;
                  k = num_waypoints;
               }
               k++;
            }
            k = 0;

            while (k <= num_waypoints) {
               if (waypoints[h].origin.x == waypoints[k].origin.x && waypoints[h].origin.z == waypoints[k].origin.z && h != k) {
                  if (waypoints[h].origin.y - 32 * (lc + 1) == waypoints[k].origin.y) {
                     k = -1;
                     lc++;
                  }
                  if (waypoints[h].origin.y + 32 * (rc + 1) == waypoints[k].origin.y) {
                     k = -1;
                     rc++;
                  }
               } else if (waypoints[h].origin.x == waypoints[k].origin.x && waypoints[h].origin.z - 16 <= waypoints[k].origin.z && waypoints[h].origin.z + 16 >= waypoints[k].origin.z && !lu && !ld && i != k) {
                  if (waypoints[h].origin.y - 32 * (lc + 1) == waypoints[k].origin.y) {
                     ll = k;
                     k = -1;
                     lc++;
                     if (waypoints[h].origin.z <= waypoints[k].origin.z)
                        ld = true;
                     else
                        lu = true;
                  }
               } else if (waypoints[h].origin.x == waypoints[k].origin.x && waypoints[h].origin.z - 16 <= waypoints[k].origin.z && waypoints[h].origin.z + 16 >= waypoints[k].origin.z && !ru && !rd && i != k) {
                  if (waypoints[h].origin.y + 32 * (rc + 1) == waypoints[k].origin.y) {
                     lr = k;
                     k = -1;
                     rc++;
                     if (waypoints[h].origin.z <= waypoints[k].origin.z)
                        rd = true;
                     else
                        ru = true;
                  }
               } else if ((waypoints[h].origin.x == waypoints[k].origin.x && (waypoints[ll].origin.z - 16 <= waypoints[k].origin.z && waypoints[ll].origin.z + 16 >= waypoints[k].origin.z)) ||
                          (waypoints[lr].origin.z - 16 <= waypoints[k].origin.z && waypoints[lr].origin.z + 16 >= waypoints[k].origin.z && (ru || rd) && (ld || lu) && i != k)) {
                  if (waypoints[h].origin.y - 32 * (lc + 1) == waypoints[k].origin.y) {
                     if (waypoints[h].origin.z <= waypoints[k].origin.z && ld) {
                        ll = k;
                        k = -1;
                        lc++;
                     } else if (lu) {
                        ll = k;
                        k = -1;
                        lc++;
                     }
                  }
                  if (waypoints[h].origin.y + 32 * (rc + 1) == waypoints[k].origin.y) {
                     if (waypoints[h].origin.z <= waypoints[k].origin.z && rd) {
                        lr = k;
                        k = -1;
                        rc++;
                     } else if (ru) {
                        lr = k;
                        k = -1;
                        rc++;
                     }
                  }
               }
               k++;
            }
            // for each area
            // scan ever other one, and see if it can be merged with it :D

            // as we expanded each area in the y direction earlier
            // we must now merge them in the x direction
            bool expanded = true;
            while (expanded) {
               expanded = false;
               for (j = 0; j <= num_areas; j++) {
                  if (!(areas[j].flags & W_FL_DELETED)) {
                     if ((areas[j].flags & A_FL_1) == A_FL_1 && (areas[j].flags & A_FL_2) == A_FL_2 && (areas[j].flags & A_FL_3) == A_FL_3 && (areas[j].flags & A_FL_4) == A_FL_4) {
                        if (i != j) {
                           if (areas[i].a == areas[j].d && areas[i].b == areas[j].c) {
                              r = 0;
                              l = 0;
                              k = 0;
                              h = 0;
                              while (k <= num_waypoints) {
                                 if (waypoints[k].origin == areas[j].d + Vector(-16, 16, 0)) {
                                    h = k;
                                    k = num_waypoints;
                                 }
                                 k++;
                              }
                              k = 0;
                              while (k <= num_waypoints) {
                                 if (waypoints[h].origin.x == waypoints[k].origin.x && waypoints[h].origin.z == waypoints[k].origin.z && h != k) {
                                    if (waypoints[h].origin.y - 32 * (l + 1) == waypoints[k].origin.y) {
                                       k = -1;
                                       l++;
                                    }
                                    if (waypoints[h].origin.y + 32 * (r + 1) == waypoints[k].origin.y) {
                                       k = -1;
                                       r++;
                                    }
                                 } else if (waypoints[h].origin.x == waypoints[k].origin.x && waypoints[h].origin.z - 16 <= waypoints[k].origin.z && waypoints[h].origin.z + 16 >= waypoints[k].origin.z && !lu && !ld && i != k) {
                                    if (waypoints[h].origin.y - 32 * (l + 1) == waypoints[k].origin.y) {
                                       ll = k;
                                       k = -1;
                                       l++;
                                       if (waypoints[h].origin.z <= waypoints[k].origin.z)
                                          ld = true;
                                       else
                                          lu = true;
                                    }
                                 } else if (waypoints[h].origin.x == waypoints[k].origin.x && waypoints[h].origin.z - 16 <= waypoints[k].origin.z && waypoints[h].origin.z + 16 >= waypoints[k].origin.z && !ru && !rd && i != k) {
                                    if (waypoints[h].origin.y + 32 * (r + 1) == waypoints[k].origin.y) {
                                       lr = k;
                                       k = -1;
                                       r++;
                                       if (waypoints[h].origin.z <= waypoints[k].origin.z)
                                          rd = true;
                                       else
                                          ru = true;
                                    }
                                 } else if ((waypoints[h].origin.x == waypoints[k].origin.x && (waypoints[ll].origin.z - 16 <= waypoints[k].origin.z && waypoints[ll].origin.z + 16 >= waypoints[k].origin.z)) ||
                                            (waypoints[lr].origin.z - 16 <= waypoints[k].origin.z && waypoints[lr].origin.z + 16 >= waypoints[k].origin.z && (ru || rd) && (ld || lu) && i != k)) {
                                    if (waypoints[h].origin.y - 32 * (l + 1) == waypoints[k].origin.y) {
                                       if (waypoints[h].origin.z <= waypoints[k].origin.z && ld) {
                                          ll = k;
                                          k = -1;
                                          l++;
                                       } else if (lu) {
                                          ll = k;
                                          k = -1;
                                          l++;
                                       }
                                    }
                                    if (waypoints[h].origin.y + 32 * (r + 1) == waypoints[k].origin.y) {
                                       if (waypoints[h].origin.z <= waypoints[k].origin.z && rd) {
                                          lr = k;
                                          k = -1;
                                          r++;
                                       } else if (ru) {
                                          lr = k;
                                          k = -1;
                                          r++;
                                       }
                                    }
                                 }
                                 k++;
                              }
                              if (rc == r && lc == l) {
                                 areas[i].a = areas[j].a;
                                 areas[i].b = areas[j].b;
                                 areas[j].flags = W_FL_DELETED;
                                 expanded = true;
                              }
                           }
                        }
                     }
                  }
               }
            }
         }
      }
   }
}

void AreaAutoMerge() {
   int i, j, k;
   // first order the areas into largest areas first [NOT DONE]
   // second, take those areas in order of size (largest first) and [NOT DONE]
   // a) find an area adjacent that expands the area in its smallest direction or
   // b) is just adjacent (and can't find/do a)
   // only group if you can find 2 more areas that make the first 2 into
   // a big square

   // work purely with areas...
   // could use wpts aswell, but loading a saved area grid won't
   // include them
   bool a, b, c, d;
   constexpr int stk_sz = 512;
   int stk[stk_sz];
   int stk_cnt;

   for (i = 0; i <= num_areas; i++) {
      if (!(areas[i].flags & W_FL_DELETED)) {
         if ((areas[i].flags & A_FL_1) == A_FL_1 && (areas[i].flags & A_FL_2) == A_FL_2 && (areas[i].flags & A_FL_3) == A_FL_3 && (areas[i].flags & A_FL_4) == A_FL_4) {
            a = false;
            b = false;
            c = false;
            d = false;
            for (j = 0; j <= num_areas; j++) {
               if (!(areas[j].flags & W_FL_DELETED) && i != j) {
                  if ((areas[j].flags & A_FL_1) == A_FL_1 && (areas[j].flags & A_FL_2) == A_FL_2 && (areas[j].flags & A_FL_3) == A_FL_3 && (areas[j].flags & A_FL_4) == A_FL_4) {
                     if (areas[j].a == areas[i].a || areas[j].b == areas[i].a || areas[j].c == areas[i].a || areas[j].d == areas[i].a)
                        a = true;
                     if (areas[j].a == areas[i].b || areas[j].b == areas[i].b || areas[j].c == areas[i].b || areas[j].d == areas[i].b)
                        b = true;
                     if (areas[j].a == areas[i].c || areas[j].b == areas[i].c || areas[j].c == areas[i].c || areas[j].d == areas[i].c)
                        c = true;
                     if (areas[j].a == areas[i].d || areas[j].b == areas[i].d || areas[j].c == areas[i].d || areas[j].d == areas[i].d)
                        d = true;
                  }
               }
            }

            // corner areas will have 1 bool (out of a-c) false only
            if ((!a && b && c && d) || (a && !b && c && d) || (a && b && !c && d) || (a && b && c && !d)) {
               // Vector aa,bb;
               // now find all the areas we can merge this one with
               bool merged = true;
               while (merged) {
                  stk_cnt = 0;
                  merged = false;
                  if (areas[i].d.x - areas[i].a.x > areas[i].b.y - areas[i].a.y) {
                     // x>y so expand in the y direction
                     for (j = 0; j <= num_areas; j++) {
                        if (!(areas[j].flags & W_FL_DELETED) && i != j) {
                           if ((areas[j].flags & A_FL_1) == A_FL_1 && (areas[j].flags & A_FL_2) == A_FL_2 && (areas[j].flags & A_FL_3) == A_FL_3 && (areas[j].flags & A_FL_4) == A_FL_4) {
                              if (!a || !d) {
                                 if (areas[j].a == areas[i].b && areas[j].d == areas[i].c) {
                                    areas[i].b = areas[j].b;
                                    areas[i].c = areas[j].c;
                                    areas[j].flags = W_FL_DELETED;
                                    merged = true;
                                    j = num_areas;
                                 }
                                 // else to multiple merge
                                 else {
                                    // start
                                    if (stk_cnt == 0 && areas[j].a == areas[i].b && areas[j].d.x < areas[i].c.x) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    } else if (stk_cnt != 0) {
                                       // middle
                                       if (stk_cnt < stk_sz && areas[j].d.x < areas[i].c.x && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                          stk[stk_cnt] = j;
                                          stk_cnt++;
                                          j = -1;
                                       }
                                       // end
                                       if (areas[j].d == areas[i].c && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                          areas[i].b = areas[stk[0]].b;
                                          areas[i].c = areas[j].c;
                                          areas[j].flags = W_FL_DELETED;
                                          for (k = 0; k < stk_cnt; k++) {
                                             areas[stk[k]].flags = W_FL_DELETED;
                                          }
                                          merged = true;
                                          j = num_areas;
                                       }
                                    }
                                 }
                              }
                              if (!b || !c) {
                                 if (areas[j].b == areas[i].a && areas[j].c == areas[i].d) {
                                    areas[i].a = areas[j].a;
                                    areas[i].d = areas[j].d;
                                    areas[j].flags = W_FL_DELETED;
                                    merged = true;
                                    j = num_areas;
                                 }
                                 // else to multiple merge
                                 else {
                                    // start
                                    if (stk_cnt == 0 && areas[j].b == areas[i].a && areas[j].c.x < areas[i].d.x) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    } else if (stk_cnt != 0) {
                                       // middle
                                       if (stk_cnt < stk_sz && areas[j].c.x < areas[i].d.x && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                          stk[stk_cnt] = j;
                                          stk_cnt++;
                                          j = -1;
                                       }
                                       // end
                                       if (areas[j].c == areas[i].d && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                          areas[i].a = areas[stk[0]].a;
                                          areas[i].d = areas[j].d;
                                          areas[j].flags = W_FL_DELETED;
                                          for (k = 0; k < stk_cnt; k++) {
                                             areas[stk[k]].flags = W_FL_DELETED;
                                          }
                                          merged = true;
                                          j = num_areas;
                                       }
                                    }
                                 }
                              }
                           }
                        }
                     }
                  } else {
                     // x<=y so expand in the x direction
                     for (j = 0; j <= num_areas; j++) {
                        if (!(areas[j].flags & W_FL_DELETED) && i != j) {
                           if ((areas[j].flags & A_FL_1) == A_FL_1 && (areas[j].flags & A_FL_2) == A_FL_2 && (areas[j].flags & A_FL_3) == A_FL_3 && (areas[j].flags & A_FL_4) == A_FL_4) {
                              if (!a || !b) {
                                 if (areas[j].a == areas[i].d && areas[j].b == areas[i].c) {
                                    areas[i].d = areas[j].d;
                                    areas[i].c = areas[j].c;
                                    areas[j].flags = W_FL_DELETED;
                                    merged = true;
                                    j = num_areas;
                                 }
                                 // else to multiple merge
                                 else {
                                    // start
                                    if (stk_cnt == 0 && areas[j].a == areas[i].d && areas[j].b.y < areas[i].c.y) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    } else if (stk_cnt != 0) {
                                       // middle
                                       if (stk_cnt < stk_sz && areas[j].b.y < areas[i].c.y && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                          stk[stk_cnt] = j;
                                          stk_cnt++;
                                          j = -1;
                                       }
                                       // end
                                       if (areas[j].b == areas[i].c && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                          areas[i].d = areas[stk[0]].d;
                                          areas[i].c = areas[j].c;
                                          areas[j].flags = W_FL_DELETED;
                                          for (k = 0; k < stk_cnt; k++) {
                                             areas[stk[k]].flags = W_FL_DELETED;
                                          }
                                          merged = true;
                                          j = num_areas;
                                       }
                                    }
                                 }
                              }
                              if (!d || !c) {
                                 if (areas[j].d == areas[i].a && areas[j].c == areas[i].b) {
                                    areas[i].a = areas[j].a;
                                    areas[i].b = areas[j].b;
                                    areas[j].flags = W_FL_DELETED;
                                    merged = true;
                                    j = num_areas;
                                 }
                                 // else to multiple merge
                                 else {
                                    // start
                                    if (stk_cnt == 0 && areas[j].d == areas[i].a && areas[j].c.y < areas[i].b.y) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    } else if (stk_cnt != 0) {
                                       // middle
                                       if (stk_cnt < stk_sz && areas[j].c.y < areas[i].b.y && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                          stk[stk_cnt] = j;
                                          stk_cnt++;
                                          j = -1;
                                       }
                                       // end
                                       if (areas[j].c == areas[i].b && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                          areas[i].a = areas[stk[0]].a;
                                          areas[i].b = areas[j].b;
                                          areas[j].flags = W_FL_DELETED;
                                          for (k = 0; k < stk_cnt; k++) {
                                             areas[stk[k]].flags = W_FL_DELETED;
                                          }
                                          merged = true;
                                          j = num_areas;
                                       }
                                    }
                                 }
                              }
                           }
                        }
                     }
                  }
               }
               // end merged
            }
         }
      }
   }

   // clear the remaining shit up!!
   for (i = 0; i <= num_areas; i++) {
      if (!(areas[i].flags & W_FL_DELETED)) {
         if ((areas[i].flags & A_FL_1) == A_FL_1 && (areas[i].flags & A_FL_2) == A_FL_2 && (areas[i].flags & A_FL_3) == A_FL_3 && (areas[i].flags & A_FL_4) == A_FL_4) {
            a = false;
            b = false;
            c = false;
            d = false;
            for (j = 0; j <= num_areas; j++) {
               if (!(areas[j].flags & W_FL_DELETED) && i != j) {
                  if ((areas[j].flags & A_FL_1) == A_FL_1 && (areas[j].flags & A_FL_2) == A_FL_2 && (areas[j].flags & A_FL_3) == A_FL_3 && (areas[j].flags & A_FL_4) == A_FL_4) {
                     if (areas[j].a == areas[i].a || areas[j].b == areas[i].a || areas[j].c == areas[i].a || areas[j].d == areas[i].a)
                        a = true;
                     if (areas[j].a == areas[i].b || areas[j].b == areas[i].b || areas[j].c == areas[i].b || areas[j].d == areas[i].b)
                        b = true;
                     if (areas[j].a == areas[i].c || areas[j].b == areas[i].c || areas[j].c == areas[i].c || areas[j].d == areas[i].c)
                        c = true;
                     if (areas[j].a == areas[i].d || areas[j].b == areas[i].d || areas[j].c == areas[i].d || areas[j].d == areas[i].d)
                        d = true;
                  }
               }
            }

            // corner areas will have 1 bool (out of a-c) false only
            if ((!a && b && c && d) || (a && !b && c && d) || (a && b && !c && d) || (a && b && c && !d) || (!a && !b && c && d) || (a && b && !c && !d) || (a && !b && !c && d) || (!a && b && c && !d)) {
               // Vector aa,bb;
               // now find all the areas we can merge this one with
               bool merged = true;
               while (merged) {
                  stk_cnt = 0;
                  merged = false;
                  if (areas[i].d.x - areas[i].a.x > areas[i].b.y - areas[i].a.y) {
                     // x>y so expand in the y direction
                     for (j = 0; j <= num_areas; j++) {
                        if (!(areas[j].flags & W_FL_DELETED) && i != j) {
                           if ((areas[j].flags & A_FL_1) == A_FL_1 && (areas[j].flags & A_FL_2) == A_FL_2 && (areas[j].flags & A_FL_3) == A_FL_3 && (areas[j].flags & A_FL_4) == A_FL_4) {
                              if (!a || !d) {
                                 if (areas[j].a == areas[i].b && areas[j].d == areas[i].c) {
                                    areas[i].b = areas[j].b;
                                    areas[i].c = areas[j].c;
                                    areas[j].flags = W_FL_DELETED;
                                    merged = true;
                                    j = num_areas;
                                 }
                                 // else to multiple merge
                                 else {
                                    // start
                                    if (stk_cnt == 0 && areas[j].a == areas[i].b && areas[j].d.x < areas[i].c.x) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    } else if (stk_cnt != 0) {
                                       // middle
                                       if (stk_cnt < stk_sz && areas[j].d.x < areas[i].c.x && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                          stk[stk_cnt] = j;
                                          stk_cnt++;
                                          j = -1;
                                       }
                                       // end
                                       if (areas[j].d == areas[i].c && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                          areas[i].b = areas[stk[0]].b;
                                          areas[i].c = areas[j].c;
                                          areas[j].flags = W_FL_DELETED;
                                          for (k = 0; k < stk_cnt; k++) {
                                             areas[stk[k]].flags = W_FL_DELETED;
                                          }
                                          merged = true;
                                          j = num_areas;
                                       }
                                    }
                                 }
                              }
                              if (!b || !c) {
                                 if (areas[j].b == areas[i].a && areas[j].c == areas[i].d) {
                                    areas[i].a = areas[j].a;
                                    areas[i].d = areas[j].d;
                                    areas[j].flags = W_FL_DELETED;
                                    merged = true;
                                    j = num_areas;
                                 }
                                 // else to multiple merge
                                 else {
                                    // start
                                    if (stk_cnt == 0 && areas[j].b == areas[i].a && areas[j].c.x < areas[i].d.x) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    } else if (stk_cnt != 0) {
                                       // middle
                                       if (stk_cnt < stk_sz && areas[j].c.x < areas[i].d.x && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                          stk[stk_cnt] = j;
                                          stk_cnt++;
                                          j = -1;
                                       }
                                       // end
                                       if (areas[j].c == areas[i].d && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                          areas[i].a = areas[stk[0]].a;
                                          areas[i].d = areas[j].d;
                                          areas[j].flags = W_FL_DELETED;
                                          for (k = 0; k < stk_cnt; k++) {
                                             areas[stk[k]].flags = W_FL_DELETED;
                                          }
                                          merged = true;
                                          j = num_areas;
                                       }
                                    }
                                 }
                              }
                           }
                        }
                     }
                  } else {
                     // x<=y so expand in the x direction
                     for (j = 0; j <= num_areas; j++) {
                        if (!(areas[j].flags & W_FL_DELETED) && i != j) {
                           if ((areas[j].flags & A_FL_1) == A_FL_1 && (areas[j].flags & A_FL_2) == A_FL_2 && (areas[j].flags & A_FL_3) == A_FL_3 && (areas[j].flags & A_FL_4) == A_FL_4) {
                              if (!a || !b) {
                                 if (areas[j].a == areas[i].d && areas[j].b == areas[i].c) {
                                    areas[i].d = areas[j].d;
                                    areas[i].c = areas[j].c;
                                    areas[j].flags = W_FL_DELETED;
                                    merged = true;
                                    j = num_areas;
                                 }
                                 // else to multiple merge
                                 else {
                                    // start
                                    if (stk_cnt == 0 && areas[j].a == areas[i].d && areas[j].b.y < areas[i].c.y) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    } else if (stk_cnt != 0) {
                                       // middle
                                       if (stk_cnt < stk_sz && areas[j].b.y < areas[i].c.y && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                          stk[stk_cnt] = j;
                                          stk_cnt++;
                                          j = -1;
                                       }
                                       // end
                                       if (areas[j].b == areas[i].c && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                          areas[i].d = areas[stk[0]].d;
                                          areas[i].c = areas[j].c;
                                          areas[j].flags = W_FL_DELETED;
                                          for (k = 0; k < stk_cnt; k++) {
                                             areas[stk[k]].flags = W_FL_DELETED;
                                          }
                                          merged = true;
                                          j = num_areas;
                                       }
                                    }
                                 }
                              }
                              if (!d || !c) {
                                 if (areas[j].d == areas[i].a && areas[j].c == areas[i].b) {
                                    areas[i].a = areas[j].a;
                                    areas[i].b = areas[j].b;
                                    areas[j].flags = W_FL_DELETED;
                                    merged = true;
                                    j = num_areas;
                                 }
                                 // else to multiple merge
                                 else {
                                    // start
                                    if (stk_cnt == 0 && areas[j].d == areas[i].a && areas[j].c.y < areas[i].b.y) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    } else if (stk_cnt != 0) {
                                       // middle
                                       if (stk_cnt < stk_sz && areas[j].c.y < areas[i].b.y && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                          stk[stk_cnt] = j;
                                          stk_cnt++;
                                          j = -1;
                                       }
                                       // end
                                       if (areas[j].c == areas[i].b && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                          areas[i].a = areas[stk[0]].a;
                                          areas[i].b = areas[j].b;
                                          areas[j].flags = W_FL_DELETED;
                                          for (k = 0; k < stk_cnt; k++) {
                                             areas[stk[k]].flags = W_FL_DELETED;
                                          }
                                          merged = true;
                                          j = num_areas;
                                       }
                                    }
                                 }
                              }
                           }
                        }
                     }
                  }
               }
               // end merged
            }
         }
      }
   }
   // and the final lot?
   for (i = 0; i <= num_areas; i++) {
      if (!(areas[i].flags & W_FL_DELETED)) {
         if ((areas[i].flags & A_FL_1) == A_FL_1 && (areas[i].flags & A_FL_2) == A_FL_2 && (areas[i].flags & A_FL_3) == A_FL_3 && (areas[i].flags & A_FL_4) == A_FL_4) {
            a = false;
            b = false;
            c = false;
            d = false;
            {
               // Vector aa,bb;
               // now find all the areas we can merge this one with
               bool merged;
               merged = true;
               while (merged) {
                  stk_cnt = 0;
                  merged = false;
                  // x>y so expand in the y direction
                  for (j = 0; j <= num_areas; j++) {
                     if (!(areas[j].flags & W_FL_DELETED) && i != j) {
                        if ((areas[j].flags & A_FL_1) == A_FL_1 && (areas[j].flags & A_FL_2) == A_FL_2 && (areas[j].flags & A_FL_3) == A_FL_3 && (areas[j].flags & A_FL_4) == A_FL_4) {
                           if (!a || !d) {
                              if (areas[j].a == areas[i].b && areas[j].d == areas[i].c) {
                                 areas[i].b = areas[j].b;
                                 areas[i].c = areas[j].c;
                                 areas[j].flags = W_FL_DELETED;
                                 merged = true;
                                 j = num_areas;
                              }
                              // else to multiple merge
                              else {
                                 // start
                                 if (stk_cnt == 0 && areas[j].a == areas[i].b && areas[j].d.x < areas[i].c.x) {
                                    stk[stk_cnt] = j;
                                    stk_cnt++;
                                    j = -1;
                                 } else if (stk_cnt != 0) {
                                    // middle
                                    if (stk_cnt < stk_sz && areas[j].d.x < areas[i].c.x && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    }
                                    // end
                                    if (areas[j].d == areas[i].c && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                       areas[i].b = areas[stk[0]].b;
                                       areas[i].c = areas[j].c;
                                       areas[j].flags = W_FL_DELETED;
                                       for (k = 0; k < stk_cnt; k++) {
                                          areas[stk[k]].flags = W_FL_DELETED;
                                       }
                                       merged = true;
                                       j = num_areas;
                                    }
                                 }
                              }
                           }
                           if (!b || !c) {
                              if (areas[j].b == areas[i].a && areas[j].c == areas[i].d) {
                                 areas[i].a = areas[j].a;
                                 areas[i].d = areas[j].d;
                                 areas[j].flags = W_FL_DELETED;
                                 merged = true;
                                 j = num_areas;
                              }
                              // else to multiple merge
                              else {
                                 // start
                                 if (stk_cnt == 0 && areas[j].b == areas[i].a && areas[j].c.x < areas[i].d.x) {
                                    stk[stk_cnt] = j;
                                    stk_cnt++;
                                    j = -1;
                                 } else if (stk_cnt != 0) {
                                    // middle
                                    if (stk_cnt < stk_sz && areas[j].c.x < areas[i].d.x && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    }
                                    // end
                                    if (areas[j].c == areas[i].d && areas[j].a == areas[stk[stk_cnt - 1]].d && areas[j].b == areas[stk[stk_cnt - 1]].c) {
                                       areas[i].a = areas[stk[0]].a;
                                       areas[i].d = areas[j].d;
                                       areas[j].flags = W_FL_DELETED;
                                       for (k = 0; k < stk_cnt; k++) {
                                          areas[stk[k]].flags = W_FL_DELETED;
                                       }
                                       merged = true;
                                       j = num_areas;
                                    }
                                 }
                              }
                           }
                        }
                     }
                  }
                  // x<=y so expand in the x direction
                  for (j = 0; j <= num_areas; j++) {
                     if (!(areas[j].flags & W_FL_DELETED) && i != j) {
                        if ((areas[j].flags & A_FL_1) == A_FL_1 && (areas[j].flags & A_FL_2) == A_FL_2 && (areas[j].flags & A_FL_3) == A_FL_3 && (areas[j].flags & A_FL_4) == A_FL_4) {
                           if (!a || !b) {
                              if (areas[j].a == areas[i].d && areas[j].b == areas[i].c) {
                                 areas[i].d = areas[j].d;
                                 areas[i].c = areas[j].c;
                                 areas[j].flags = W_FL_DELETED;
                                 merged = true;
                                 j = num_areas;
                              }
                              // else to multiple merge
                              else {
                                 // start
                                 if (stk_cnt == 0 && areas[j].a == areas[i].d && areas[j].b.y < areas[i].c.y) {
                                    stk[stk_cnt] = j;
                                    stk_cnt++;
                                    j = -1;
                                 } else if (stk_cnt != 0) {
                                    // middle
                                    if (stk_cnt < stk_sz && areas[j].b.y < areas[i].c.y && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    }
                                    // end
                                    if (areas[j].b == areas[i].c && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                       areas[i].d = areas[stk[0]].d;
                                       areas[i].c = areas[j].c;
                                       areas[j].flags = W_FL_DELETED;
                                       for (k = 0; k < stk_cnt; k++) {
                                          areas[stk[k]].flags = W_FL_DELETED;
                                       }
                                       merged = true;
                                       j = num_areas;
                                    }
                                 }
                              }
                           }
                           if (!d || !c) {
                              if (areas[j].d == areas[i].a && areas[j].c == areas[i].b) {
                                 areas[i].a = areas[j].a;
                                 areas[i].b = areas[j].b;
                                 areas[j].flags = W_FL_DELETED;
                                 merged = true;
                                 j = num_areas;
                              }
                              // else to multiple merge
                              else {
                                 // start
                                 if (stk_cnt == 0 && areas[j].d == areas[i].a && areas[j].c.y < areas[i].b.y) {
                                    stk[stk_cnt] = j;
                                    stk_cnt++;
                                    j = -1;
                                 } else if (stk_cnt != 0) {
                                    // middle
                                    if (stk_cnt < stk_sz && areas[j].c.y < areas[i].b.y && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                       stk[stk_cnt] = j;
                                       stk_cnt++;
                                       j = -1;
                                    }
                                    // end
                                    if (areas[j].c == areas[i].b && areas[j].a == areas[stk[stk_cnt - 1]].b && areas[j].d == areas[stk[stk_cnt - 1]].c) {
                                       areas[i].a = areas[stk[0]].a;
                                       areas[i].b = areas[j].b;
                                       areas[j].flags = W_FL_DELETED;
                                       for (k = 0; k < stk_cnt; k++) {
                                          areas[stk[k]].flags = W_FL_DELETED;
                                       }
                                       merged = true;
                                       j = num_areas;
                                    }
                                 }
                              }
                           }
                        }
                     }
                  }
               }
               // end merged
            }
         }
      }
   }

   // first group areas that are slopes/stairs

   // given a area, slope in either direction, aslong as
   // a)size of adjacent area matches
   // b)step is the same (more than 2 areas)
   // c)same direction as first ones found
   for (i = 0; i <= num_areas; i++) {
      if (!(areas[i].flags & W_FL_DELETED)) {
         if ((areas[i].flags & A_FL_1) == A_FL_1 && (areas[i].flags & A_FL_2) == A_FL_2 && (areas[i].flags & A_FL_3) == A_FL_3 && (areas[i].flags & A_FL_4) == A_FL_4) {
            float sx, sy;
            sx = areas[i].d.x - areas[i].a.x;
            sy = areas[i].b.y - areas[i].a.y;
            for (j = 0; j <= num_areas; j++) {
               if (!(areas[j].flags & W_FL_DELETED) && i != j) {
                  if ((areas[j].flags & A_FL_1) == A_FL_1 && (areas[j].flags & A_FL_2) == A_FL_2 && (areas[j].flags & A_FL_3) == A_FL_3 && (areas[j].flags & A_FL_4) == A_FL_4) {
                     // find neighbours to i
                     float z;

                     z = areas[j].a.z - areas[i].d.z;
                     if (areas[j].b.z - areas[i].c.z != z)
                        z = 0;
                     if (areas[j].a.x == areas[i].d.x && areas[j].a.y == areas[i].d.y && areas[j].b.x == areas[i].c.x &&
                         areas[j].b.y == areas[i].c.y
                         /* && areas[j].a.z==areas[j].d.z
                         && areas[j].b.z==areas[j].c.z*/
                         && areas[j].a.z == areas[j].b.z && areas[j].d.z == areas[j].c.z && (z != 0 && (z >= -32 && z <= 32))) {
                        // now check the size
                        // if it matches, merge
                        if (sx == areas[j].d.x - areas[j].a.x && sy == areas[j].b.y - areas[j].a.y) {
                           areas[i].d = areas[j].d;
                           areas[i].c = areas[j].c;
                           areas[j].flags = W_FL_DELETED;

                           // j=-1;
                           // now search for neighbours with the same z
                           // and same size
                           for (k = 0; k <= num_areas; k++) {
                              if (!(areas[k].flags & W_FL_DELETED) && j != k && i != k) {
                                 if ((areas[k].flags & A_FL_1) == A_FL_1 && (areas[k].flags & A_FL_2) == A_FL_2 && (areas[k].flags & A_FL_3) == A_FL_3 && (areas[k].flags & A_FL_4) == A_FL_4) {
                                    float zz;
                                    zz = areas[k].a.z - areas[i].d.z;
                                    if (areas[k].b.z - areas[i].c.z != zz)
                                       zz = 0;
                                    if (areas[k].a.x == areas[i].d.x && areas[k].a.y == areas[i].d.y && areas[k].b.x == areas[i].c.x &&
                                        areas[k].b.y == areas[i].c.y
                                        /* && areas[k].a.z==areas[k].d.z
                                        && areas[k].b.z==areas[k].c.z*/
                                        && areas[k].a.z == areas[k].b.z &&
                                        areas[k].d.z == areas[k].c.z
                                        // && zz!=0 && (z==zz || z==-zz || -z==zz))
                                        && (zz != 0 && (zz >= -32 && zz <= 32))) {
                                       // now check the size
                                       // if it matches, merge
                                       if (sx == areas[k].d.x - areas[k].a.x && sy == areas[k].b.y - areas[k].a.y) {
                                          areas[i].d = areas[k].d;
                                          areas[i].c = areas[k].c;
                                          areas[k].flags = W_FL_DELETED;
                                          k = -1;
                                       }
                                    }
                                    zz = areas[k].d.z - areas[i].a.z;
                                    if (areas[k].c.z - areas[i].b.z != zz)
                                       zz = 0;
                                    if (areas[k].d.x == areas[i].a.x && areas[k].d.y == areas[i].a.y && areas[k].c.x == areas[i].b.x &&
                                        areas[k].c.y == areas[i].b.y
                                        /* && areas[k].a.z==areas[k].d.z
                                        && areas[k].b.z==areas[k].c.z*/
                                        && areas[k].a.z == areas[k].b.z &&
                                        areas[k].d.z == areas[k].c.z
                                        // && zz!=0 && (z==zz || z==-zz || -z==zz))
                                        && (zz != 0 && (zz >= -32 && zz <= 32))) {
                                       // now check the size
                                       // if it matches, merge
                                       if (sx == areas[k].d.x - areas[k].a.x && sy == areas[k].b.y - areas[k].a.y) {
                                          areas[i].a = areas[k].a;
                                          areas[i].b = areas[k].b;
                                          areas[k].flags = W_FL_DELETED;
                                          k = -1;
                                       }
                                    }
                                 }
                              }
                           }
                        }
                     }
                     // other way round
                     z = areas[j].a.z - areas[i].b.z;
                     if (areas[j].d.z - areas[i].c.z != z)
                        z = 0;
                     if (areas[j].a.x == areas[i].b.x && areas[j].a.y == areas[i].b.y && areas[j].d.x == areas[i].c.x &&
                         areas[j].d.y == areas[i].c.y
                         /* && areas[j].a.z==areas[j].d.z
                         && areas[j].b.z==areas[j].c.z*/
                         && areas[j].a.z == areas[j].b.z && areas[j].d.z == areas[j].c.z && (z != 0 && (z >= -32 && z <= 32))) {
                        // now check the size
                        // if it matches, merge
                        if (sx == areas[j].d.x - areas[j].a.x && sy == areas[j].b.y - areas[j].a.y) {
                           areas[i].b = areas[j].b;
                           areas[i].c = areas[j].c;
                           areas[j].flags = W_FL_DELETED;

                           // j=-1;
                           // now search for neighbours with the same z
                           // and same size
                           for (k = 0; k <= num_areas; k++) {
                              if (!(areas[k].flags & W_FL_DELETED) && j != k && i != k) {
                                 if ((areas[k].flags & A_FL_1) == A_FL_1 && (areas[k].flags & A_FL_2) == A_FL_2 && (areas[k].flags & A_FL_3) == A_FL_3 && (areas[k].flags & A_FL_4) == A_FL_4) {
                                    float zz;
                                    zz = areas[k].a.z - areas[i].d.z;
                                    if (areas[k].b.z - areas[i].c.z != zz)
                                       zz = 0;
                                    if (areas[k].a.x == areas[i].b.x && areas[k].a.y == areas[i].b.y && areas[k].d.x == areas[i].c.x &&
                                        areas[k].d.y == areas[i].c.y
                                        /* && areas[k].a.z==areas[k].d.z
                                        && areas[k].b.z==areas[k].c.z*/
                                        && areas[k].a.z == areas[k].b.z &&
                                        areas[k].d.z == areas[k].c.z
                                        /* && zz!=0
                                        && (z==zz || z==-zz || -z==zz))*/
                                        && (zz != 0 && (zz >= -32 && zz <= 32))) {
                                       // now check the size
                                       // if it matches, merge
                                       if (sx == areas[k].d.x - areas[k].a.x && sy == areas[k].b.y - areas[k].a.y) {
                                          areas[i].b = areas[k].b;
                                          areas[i].c = areas[k].c;
                                          areas[k].flags = W_FL_DELETED;
                                          k = -1;
                                       }
                                    }
                                    zz = areas[k].d.z - areas[i].a.z;
                                    if (areas[k].c.z - areas[i].b.z != zz)
                                       zz = 0;
                                    if (areas[k].b.x == areas[i].a.x && areas[k].b.y == areas[i].a.y && areas[k].c.x == areas[i].d.x &&
                                        areas[k].c.y == areas[i].d.y
                                        /*&& areas[k].a.z==areas[k].d.z
                                        && areas[k].b.z==areas[k].c.z*/
                                        && areas[k].a.z == areas[k].b.z &&
                                        areas[k].d.z == areas[k].c.z
                                        //&& zz!=0 && (z==zz || z==-zz || -z==zz))
                                        && (zz != 0 && (zz >= -32 && zz <= 32))) {
                                       // now check the size
                                       // if it matches, merge
                                       if (sx == areas[k].d.x - areas[k].a.x && sy == areas[k].b.y - areas[k].a.y) {
                                          areas[i].a = areas[k].a;
                                          areas[i].d = areas[k].d;
                                          areas[k].flags = W_FL_DELETED;
                                          k = -1;
                                       }
                                    }
                                 }
                              }
                           }
                        }
                     }
                  }
               }
            }
         }
      }
   }
}

// ProcessCommanderList - This command will read in a list of
// commanders from a text file.
// The id's in this file will be used to determine players that have
// access to command
// the bots, as in assign roles, classes positions...
void ProcessCommanderList() {
   char msg[255];
   char buffer[80];
   char filename[255];
   //// delete dynamic memory
   // LIter<char *> iter(&commanders);
   // for(iter.begin(); !iter.end(); ++iter)
   //{
   //}
   commanders.clear();
   constexpr char invalidChars[] = " abcdefghijklmnopqrstuvwxyz,./<>?;'\"[]{}-=+!@#$%^&*()";

   UTIL_BuildFileName(filename, 255, "foxbot_commanders.txt", nullptr);
   FILE *inFile = fopen(filename, "r");

   if (inFile) {
      if (IS_DEDICATED_SERVER())
         printf("[Config] Reading foxbot_commanders.txt\n");
      else {
         sprintf(msg, "[Config] Reading foxbot_commanders.txt\n");
         ALERT(at_console, msg);
      }
   } else {
      if (IS_DEDICATED_SERVER())
         printf("[Config] Couldn't open foxbot_commanders.txt\n");
      else {
         sprintf(msg, "[Config] Couldn't open foxbot_commanders.txt\n");
         ALERT(at_console, msg);
      }
      return;
   }

   // Read the file, line by line.
   while (UTIL_ReadFileLine(buffer, 80, inFile)) {
      // Skip lines that begin with comments
      if (static_cast<int>(strlen(buffer)) > 2) {
         if (buffer[0] == '/' && buffer[1] == '/')
            continue;
      }
      bool valid = true;

      // Search for invalid characters in the read string.
      // strlen is being called too many times in the for loop - [APG]RoboCop[CL]
      for (int i = 0; i < static_cast<int>(strlen(buffer)); i++) {
          for (int j = 0; j < static_cast<int>(strlen(invalidChars)); j++) {
              const char ch = invalidChars[j];

              if (strchr(buffer, ch)) {
                  valid = false;
                  if (IS_DEDICATED_SERVER())
                      printf("[Config] foxbot_commanders.txt : Invalid Character %c\n", ch);
                  else {
                      sprintf(msg, "[Config] foxbot_commanders.txt : Invalid Character %c\n", ch);
                      ALERT(at_console, msg);
                  }
              }
          }
      }

      // The read string is valid enough.
      if (valid) {
         char *uId = new char[80];
         strcpy(uId, buffer);

         // Get rid of line feeds
         if (uId[strlen(uId) - 1] == '\n' || uId[strlen(uId) - 1] == '\r' || uId[strlen(uId) - 1] == EOF) /*{
                 uId[strlen(uId) - 1] = '\0';
         }*/

            fp = UTIL_OpenFoxbotLog();
         if (fp != nullptr) {
            fprintf(fp, "LOAD USERID: %s\n", uId);
            fclose(fp);
         }

         commanders.addTail(uId);
         if (IS_DEDICATED_SERVER())
            printf("[Config] foxbot_commanders.txt : Loaded User %s\n", buffer);
         else {
            sprintf(msg, "[Config] foxbot_commanders.txt : Loaded User %s\n", buffer);
            ALERT(at_console, msg);
         }
      }
   }
   // Report status
   if (IS_DEDICATED_SERVER())
      printf("[Config] foxbot_commanders.txt : Loaded %d users\n", commanders.size());
   else {
      sprintf(msg, "[Config] foxbot_commanders.txt : Loaded %d users\n", commanders.size());
      ALERT(at_console, msg);
   }
   fclose(inFile);
}