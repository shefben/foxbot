#ifndef BOT_FSM_H
#define BOT_FSM_H

#include "bot_job_think.h"

enum BotState {
    BOT_STATE_IDLE = 0,
    BOT_STATE_ROAM,
    BOT_STATE_ATTACK,
    BOT_STATE_DEFEND,
    BOT_STATE_COUNT
};

struct BotFSM {
    BotState current;
    BotState previous;
    float transition[BOT_STATE_COUNT][BOT_STATE_COUNT];
    unsigned counts[BOT_STATE_COUNT][BOT_STATE_COUNT];
};

enum MoveState {
    MOVE_NORMAL = 0,
    MOVE_HEAL,
    MOVE_STAB,
    MOVE_STATE_COUNT
};

struct MoveFSM {
    MoveState current;
    MoveState previous;
    float transition[MOVE_STATE_COUNT][MOVE_STATE_COUNT];
    unsigned counts[MOVE_STATE_COUNT][MOVE_STATE_COUNT];
};

struct JobFSM {
    int current;
    int previous;
    float transition[JOB_TYPE_TOTAL][JOB_TYPE_TOTAL];
    unsigned counts[JOB_TYPE_TOTAL][JOB_TYPE_TOTAL];
};

enum WeaponState {
    WEAPON_PRIMARY = 0,
    WEAPON_SECONDARY,
    WEAPON_MELEE,
    WEAPON_GRENADE,
    WEAPON_STATE_COUNT
};

struct WeaponFSM {
    WeaponState current;
    WeaponState previous;
    float transition[WEAPON_STATE_COUNT][WEAPON_STATE_COUNT];
    unsigned counts[WEAPON_STATE_COUNT][WEAPON_STATE_COUNT];
};

enum ChatState {
    CHAT_IDLE = 0,
    CHAT_GREET,
    CHAT_KILL,
    CHAT_DEATH,
    CHAT_STATE_COUNT
};

struct ChatFSM {
    ChatState current;
    ChatState previous;
    float transition[CHAT_STATE_COUNT][CHAT_STATE_COUNT];
    unsigned counts[CHAT_STATE_COUNT][CHAT_STATE_COUNT];
};

enum AimState {
    AIM_HEAD = 0,
    AIM_BODY,
    AIM_FEET,
    AIM_STATE_COUNT
};

struct AimFSM {
    AimState current;
    AimState previous;
    float transition[AIM_STATE_COUNT][AIM_STATE_COUNT];
    unsigned counts[AIM_STATE_COUNT][AIM_STATE_COUNT];
};

enum CombatState {
    COMBAT_IDLE = 0,
    COMBAT_APPROACH,
    COMBAT_ATTACK,
    COMBAT_RETREAT,
    COMBAT_STATE_COUNT
};

struct CombatFSM {
    CombatState current;
    CombatState previous;
    float transition[COMBAT_STATE_COUNT][COMBAT_STATE_COUNT];
    unsigned counts[COMBAT_STATE_COUNT][COMBAT_STATE_COUNT];
};

enum ReactionState {
    REACT_CALM = 0,
    REACT_ALERT,
    REACT_PANIC,
    REACT_STATE_COUNT
};

struct ReactionFSM {
    ReactionState current;
    ReactionState previous;
    float transition[REACT_STATE_COUNT][REACT_STATE_COUNT];
    unsigned counts[REACT_STATE_COUNT][REACT_STATE_COUNT];
};

enum NavState {
    NAV_STRAIGHT = 0,
    NAV_STRAFE,
    NAV_JUMP,
    NAV_STATE_COUNT
};

struct NavFSM {
    NavState current;
    NavState previous;
    float transition[NAV_STATE_COUNT][NAV_STATE_COUNT];
    unsigned counts[NAV_STATE_COUNT][NAV_STATE_COUNT];
};

void BotFSMInit(BotFSM *fsm, BotState initial);
BotState BotFSMNextState(BotFSM *fsm);
void MoveFSMInit(MoveFSM *fsm, MoveState initial);
MoveState MoveFSMNextState(MoveFSM *fsm);
void JobFSMInit(JobFSM *fsm, int initial);
int JobFSMNextState(JobFSM *fsm);
void WeaponFSMInit(WeaponFSM *fsm, WeaponState initial);
WeaponState WeaponFSMNextState(WeaponFSM *fsm);
void ChatFSMInit(ChatFSM *fsm, ChatState initial);
ChatState ChatFSMNextState(ChatFSM *fsm);
void CombatFSMInit(CombatFSM *fsm, CombatState initial);
CombatState CombatFSMNextState(CombatFSM *fsm);
void BotApplyCombatState(bot_t *pBot);
void AimFSMInit(AimFSM *fsm, AimState initial);
AimState AimFSMNextState(AimFSM *fsm);
void BotUpdateAim(bot_t *pBot);

void NavFSMInit(NavFSM *fsm, NavState initial);
NavState NavFSMNextState(NavFSM *fsm);
void BotUpdateNavigation(bot_t *pBot);
void BotApplyNavState(bot_t *pBot);

void ReactionFSMInit(ReactionFSM *fsm, ReactionState initial);
ReactionState ReactionFSMNextState(ReactionFSM *fsm, float avgAllyHealth, int recentKills);
void BotUpdateReaction(bot_t *pBot);
void FSMPeriodicSave(float currentTime);

void SaveFSMCounts();
void LoadFSMCounts();

void SaveBotMetrics();
void LoadBotMetrics();

extern float gBotAccuracy[32];
extern float gBotReaction[32];

#endif // BOT_FSM_H
