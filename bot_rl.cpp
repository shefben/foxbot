#include "bot_rl.h"
#include "util.h"
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

void RL_RecordRoundEnd(BotFSM *fsm) {
    ++gStateScores[fsm->current];
}

float RL_GetStateWeight(BotState state) {
    return 1.0f + static_cast<float>(gStateScores[state]) / 10.0f;
}
