#ifndef BOT_RL_H
#define BOT_RL_H

#include "bot_fsm.h"

void RL_LoadScores();
void RL_SaveScores();
void RL_RecordRoundEnd(BotFSM *fsm);
float RL_GetStateWeight(BotState state);

#endif // BOT_RL_H
