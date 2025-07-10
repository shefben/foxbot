#ifndef BOT_RL_H
#define BOT_RL_H

#include "bot_fsm.h"
#include "bot.h"

void RL_LoadScores();
void RL_SaveScores();
void RL_RecordRoundEnd(BotFSM *fsm, bot_t *bot);
float RL_GetStateWeight(BotState state);
void RL_RecordPathResult(int from, int to, bool success);
float RL_GetPathWeight(int from, int to);

#endif // BOT_RL_H
