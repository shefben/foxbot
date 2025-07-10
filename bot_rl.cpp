#include "bot_rl.h"
#include "util.h"
#include "bot.h"

extern bot_t bots[32];
#include <cstdio>
#include <unordered_map>
#include <cstdint>
#include "compat.h"
#include <climits>

static unsigned gStateScores[BOT_STATE_COUNT];

struct PathStats {
    unsigned wins;
    unsigned total;
};

static std::unordered_map<uint64_t, PathStats> gPathStats;

static uint64_t path_key(int from, int to) {
    return (static_cast<uint64_t>(from) << 32) | static_cast<uint32_t>(to);
}

void RL_LoadScores() {
    char fname[256];
    UTIL_BuildFileName(fname, 255, (char*)"bot_rl.dat", NULL);
    FILE *fp = fopen(fname, "rb");
    if(!fp) {
        for(int i=0;i<BOT_STATE_COUNT;i++) gStateScores[i]=0;
        gPathStats.clear();
        return;
    }
    fread(gStateScores, sizeof(unsigned), BOT_STATE_COUNT, fp);
    fclose(fp);

    UTIL_BuildFileName(fname, 255, (char*)"bot_rl_paths.dat", NULL);
    fp = fopen(fname, "rb");
    if(fp) {
        unsigned count = 0;
        fread(&count, sizeof(unsigned), 1, fp);
        for(unsigned i=0;i<count;i++) {
            uint64_t key; PathStats ps{};
            fread(&key, sizeof(uint64_t), 1, fp);
            fread(&ps, sizeof(PathStats), 1, fp);
            gPathStats[key] = ps;
        }
        fclose(fp);
    }
}

void RL_SaveScores() {
    char fname[256];
    UTIL_BuildFileName(fname, 255, (char*)"bot_rl.dat", NULL);
    FILE *fp = fopen(fname, "wb");
    if(!fp) return;
    fwrite(gStateScores, sizeof(unsigned), BOT_STATE_COUNT, fp);
    fclose(fp);

    UTIL_BuildFileName(fname, 255, (char*)"bot_rl_paths.dat", NULL);
    fp = fopen(fname, "wb");
    if(fp) {
        unsigned count = gPathStats.size();
        fwrite(&count, sizeof(unsigned), 1, fp);
        for(const auto &p : gPathStats) {
            fwrite(&p.first, sizeof(uint64_t), 1, fp);
            fwrite(&p.second, sizeof(PathStats), 1, fp);
        }
        fclose(fp);
    }
}

void RL_RecordRoundEnd(BotFSM *fsm, bot_t *bot) {
    ++gStateScores[fsm->current];

    if(!bot)
        return;

    int perf = bot->roundKills - bot->roundDeaths;

    bot->accuracy += 0.01f * static_cast<float>(perf);
    bot->reaction_speed += 0.005f * static_cast<float>(perf);

    if(bot->accuracy < 0.0f) bot->accuracy = 0.0f;
    if(bot->accuracy > 1.0f) bot->accuracy = 1.0f;
    if(bot->reaction_speed < 0.0f) bot->reaction_speed = 0.0f;
    if(bot->reaction_speed > 1.0f) bot->reaction_speed = 1.0f;

    bot->roundKills = 0;
    bot->roundDeaths = 0;

    int idx = bot - bots;
    if(idx >= 0 && idx < 32) {
        gBotAccuracy[idx] = bot->accuracy;
        gBotReaction[idx] = bot->reaction_speed;
    }

    bot->scoreAtSpawn = static_cast<int>(bot->pEdict->v.frags);
}

float RL_GetStateWeight(BotState state) {
    return 1.0f + static_cast<float>(gStateScores[state]) / 10.0f;
}

void RL_RecordPathResult(int from, int to, bool success) {
    if(from < 0 || to < 0)
        return;
    uint64_t key = path_key(from, to);
    PathStats &ps = gPathStats[key];
    if(ps.total < UINT_MAX) ps.total++;
    if(success && ps.wins < UINT_MAX) ps.wins++;
}

float RL_GetPathWeight(int from, int to) {
    if(from < 0 || to < 0)
        return 1.0f;
    uint64_t key = path_key(from, to);
    auto it = gPathStats.find(key);
    if(it == gPathStats.end() || it->second.total == 0)
        return 1.0f;
    return 1.0f + static_cast<float>(it->second.wins) / it->second.total;
}
