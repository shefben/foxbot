#include "bot_rl.h"
#include "util.h"
#include "bot.h"

extern bot_t bots[32];
#include <cstdio>

static unsigned gStateScores[BOT_STATE_COUNT];

void RL_LoadScores() {
    char fname[256];
    UTIL_BuildFileName(fname, 255, (char*)"bot_rl.dat", NULL);
    FILE *fp = fopen(fname, "rb");
    if(!fp) {
        for(int i=0;i<BOT_STATE_COUNT;i++) gStateScores[i]=0;
        return;
    }
    fread(gStateScores, sizeof(unsigned), BOT_STATE_COUNT, fp);
    fclose(fp);
}

void RL_SaveScores() {
    char fname[256];
    UTIL_BuildFileName(fname, 255, (char*)"bot_rl.dat", NULL);
    FILE *fp = fopen(fname, "wb");
    if(!fp) return;
    fwrite(gStateScores, sizeof(unsigned), BOT_STATE_COUNT, fp);
    fclose(fp);
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
