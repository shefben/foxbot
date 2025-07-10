#include "bot_fsm.h"
#include "bot_rl.h"
#include "bot.h"
#include "bot_job_think.h"
#include "bot_markov.h"
#include "bot_job_functions.h"
#include <enginecallback.h>
#include "bot_func.h"

extern chatClass chat;
extern int bot_chat;
extern bot_t bots[32];

static unsigned gBotCounts[BOT_STATE_COUNT][BOT_STATE_COUNT];
static unsigned gMoveCounts[MOVE_STATE_COUNT][MOVE_STATE_COUNT];
static unsigned gJobCounts[JOB_TYPE_TOTAL][JOB_TYPE_TOTAL];
static unsigned gWeaponCounts[WEAPON_STATE_COUNT][WEAPON_STATE_COUNT];
static unsigned gChatCounts[CHAT_STATE_COUNT][CHAT_STATE_COUNT];
static unsigned gCombatCounts[COMBAT_STATE_COUNT][COMBAT_STATE_COUNT];
static unsigned gAimCounts[AIM_STATE_COUNT][AIM_STATE_COUNT];
static unsigned gNavCounts[NAV_STATE_COUNT][NAV_STATE_COUNT];
static unsigned gReactionCounts[REACT_STATE_COUNT][REACT_STATE_COUNT];

static const unsigned FSM_FILE_VERSION = 1;
static const unsigned METRICS_FILE_VERSION = 1;

TeamSignalType g_teamSignals[MAX_TEAMS];
float g_teamSignalExpire[MAX_TEAMS];

float gBotAccuracy[32];
float gBotReaction[32];

static void load_metrics(const char *file) {
    FILE *fp = fopen(file, "rb");
    if(!fp) {
        UTIL_BotLogPrintf("load_metrics: failed to open %s\n", file);
        for(int i=0;i<32;i++) {
            gBotAccuracy[i] = 0.5f;
            gBotReaction[i] = 0.5f;
        }
        return;
    }
    unsigned version = 0;
    if(fread(&version, sizeof(unsigned), 1, fp) != 1 || version != METRICS_FILE_VERSION) {
        UTIL_BotLogPrintf("load_metrics: version mismatch for %s\n", file);
        for(int i=0;i<32;i++) {
            gBotAccuracy[i] = 0.5f;
            gBotReaction[i] = 0.5f;
        }
        fclose(fp);
        return;
    }
    fread(gBotAccuracy, sizeof(float), 32, fp);
    fread(gBotReaction, sizeof(float), 32, fp);
    fclose(fp);
}

static void save_metrics(const char *file) {
    FILE *fp = fopen(file, "wb");
    if(!fp) {
        UTIL_BotLogPrintf("save_metrics: failed to open %s\n", file);
        return;
    }
    fwrite(&METRICS_FILE_VERSION, sizeof(unsigned), 1, fp);
    fwrite(gBotAccuracy, sizeof(float), 32, fp);
    fwrite(gBotReaction, sizeof(float), 32, fp);
    fclose(fp);
}

static void load_counts(const char *file, unsigned *counts, int states) {
    FILE *fp = fopen(file, "rb");
    if(!fp) {
        UTIL_BotLogPrintf("load_counts: failed to open %s\n", file);
        for(int i=0;i<states*states;i++) counts[i]=1;
        return;
    }
    unsigned version = 0;
    if(fread(&version, sizeof(unsigned), 1, fp) != 1 || version != FSM_FILE_VERSION) {
        UTIL_BotLogPrintf("load_counts: version mismatch for %s\n", file);
        for(int i=0;i<states*states;i++) counts[i]=1;
        fclose(fp);
        return;
    }
    fread(counts, sizeof(unsigned), states*states, fp);
    fclose(fp);
}

static void save_counts(const char *file, unsigned *counts, int states) {
    FILE *fp = fopen(file, "wb");
    if(!fp) {
        UTIL_BotLogPrintf("save_counts: failed to open %s\n", file);
        return;
    }
    fwrite(&FSM_FILE_VERSION, sizeof(unsigned), 1, fp);
    fwrite(counts, sizeof(unsigned), states*states, fp);
    fclose(fp);
}

void LoadFSMCounts() {
    char fname[256];
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_bot.dat", NULL);
    load_counts(fname, &gBotCounts[0][0], BOT_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_move.dat", NULL);
    load_counts(fname, &gMoveCounts[0][0], MOVE_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_job.dat", NULL);
    load_counts(fname, &gJobCounts[0][0], JOB_TYPE_TOTAL);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_weapon.dat", NULL);
    load_counts(fname, &gWeaponCounts[0][0], WEAPON_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_chat.dat", NULL);
    load_counts(fname, &gChatCounts[0][0], CHAT_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_combat.dat", NULL);
    load_counts(fname, &gCombatCounts[0][0], COMBAT_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_aim.dat", NULL);
    load_counts(fname, &gAimCounts[0][0], AIM_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_nav.dat", NULL);
    load_counts(fname, &gNavCounts[0][0], NAV_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_react.dat", NULL);
    load_counts(fname, &gReactionCounts[0][0], REACT_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_metrics.dat", NULL);
    load_metrics(fname);
    RL_LoadScores();
}

void SaveFSMCounts() {
    char fname[256];
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_bot.dat", NULL);
    save_counts(fname, &gBotCounts[0][0], BOT_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_move.dat", NULL);
    save_counts(fname, &gMoveCounts[0][0], MOVE_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_job.dat", NULL);
    save_counts(fname, &gJobCounts[0][0], JOB_TYPE_TOTAL);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_weapon.dat", NULL);
    save_counts(fname, &gWeaponCounts[0][0], WEAPON_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_chat.dat", NULL);
    save_counts(fname, &gChatCounts[0][0], CHAT_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_combat.dat", NULL);
    save_counts(fname, &gCombatCounts[0][0], COMBAT_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_aim.dat", NULL);
    save_counts(fname, &gAimCounts[0][0], AIM_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_nav.dat", NULL);
    save_counts(fname, &gNavCounts[0][0], NAV_STATE_COUNT);
    UTIL_BuildFileName(fname, 255, (char*)"bot_fsm_react.dat", NULL);
    save_counts(fname, &gReactionCounts[0][0], REACT_STATE_COUNT);
    RL_SaveScores();
}

void LoadBotMetrics() {
    char fname[256];
    UTIL_BuildFileName(fname, 255, (char*)"bot_metrics.dat", NULL);
    load_metrics(fname);
}

void SaveBotMetrics() {
    char fname[256];
    for(int i=0;i<32;i++) {
        gBotAccuracy[i] = bots[i].accuracy;
        gBotReaction[i] = bots[i].reaction_speed;
    }
    UTIL_BuildFileName(fname, 255, (char*)"bot_metrics.dat", NULL);
    save_metrics(fname);
}

static void BotFSMUpdateCounts(BotFSM *fsm, int from, int to) {
    ++fsm->counts[from][to];
    ++gBotCounts[from][to];
    float total = 0.0f;
    for(int j = 0; j < BOT_STATE_COUNT; ++j)
        total += fsm->counts[from][j];
    for(int j = 0; j < BOT_STATE_COUNT; ++j)
        fsm->transition[from][j] = (float)fsm->counts[from][j] / total;
}

void BotFSMInit(BotFSM *fsm, BotState initial) {
    fsm->current = fsm->previous = initial;
    for(int i = 0; i < BOT_STATE_COUNT; ++i) {
        unsigned total = 0;
        for(int j = 0; j < BOT_STATE_COUNT; ++j) {
            fsm->counts[i][j] = gBotCounts[i][j] ? gBotCounts[i][j] : 1;
            total += fsm->counts[i][j];
        }
        for(int j = 0; j < BOT_STATE_COUNT; ++j)
            fsm->transition[i][j] = (float)fsm->counts[i][j] / (float)total;
    }
}

BotState BotFSMNextState(BotFSM *fsm) {
    float weights[BOT_STATE_COUNT];
    float total = 0.0f;
    int curr = static_cast<int>(fsm->current);
    for(int j = 0; j < BOT_STATE_COUNT; ++j) {
        weights[j] = fsm->transition[curr][j] * RL_GetStateWeight(static_cast<BotState>(j));
        total += weights[j];
    }

    float r = random_float(0.0f, total);
    float acc = 0.0f;
    int next = curr;
    for(int j = 0; j < BOT_STATE_COUNT; ++j) {
        acc += weights[j];
        if(r <= acc) {
            next = j;
            break;
        }
    }
    BotFSMUpdateCounts(fsm, curr, next);
    fsm->previous = fsm->current;
    fsm->current = static_cast<BotState>(next);
    return fsm->current;
}

static void MoveFSMUpdateCounts(MoveFSM *fsm, int from, int to) {
    ++fsm->counts[from][to];
    ++gMoveCounts[from][to];
    float total = 0.0f;
    for(int j = 0; j < MOVE_STATE_COUNT; ++j)
        total += fsm->counts[from][j];
    for(int j = 0; j < MOVE_STATE_COUNT; ++j)
        fsm->transition[from][j] = (float)fsm->counts[from][j] / total;
}

void MoveFSMInit(MoveFSM *fsm, MoveState initial) {
    fsm->current = fsm->previous = initial;
    bool uniform = true;
    for(int i = 0; i < MOVE_STATE_COUNT && uniform; ++i)
        for(int j = 0; j < MOVE_STATE_COUNT && uniform; ++j)
            if(gMoveCounts[i][j] != 1)
                uniform = false;

    static const unsigned defaults[MOVE_STATE_COUNT][MOVE_STATE_COUNT] = {
        {8, 1, 1},  // from NORMAL
        {6, 3, 1},  // from HEAL
        {6, 1, 3}   // from STAB
    };

    for(int i = 0; i < MOVE_STATE_COUNT; ++i) {
        unsigned total = 0;
        for(int j = 0; j < MOVE_STATE_COUNT; ++j) {
            fsm->counts[i][j] = uniform ? defaults[i][j]
                                       : (gMoveCounts[i][j] ? gMoveCounts[i][j] : 1);
            total += fsm->counts[i][j];
        }
        for(int j = 0; j < MOVE_STATE_COUNT; ++j)
            fsm->transition[i][j] = static_cast<float>(fsm->counts[i][j]) / (float)total;
    }
}

MoveState MoveFSMNextState(MoveFSM *fsm) {
    float r = random_float(0.0f, 1.0f);
    float acc = 0.0f;
    int curr = static_cast<int>(fsm->current);
    int next = curr;
    for(int j = 0; j < MOVE_STATE_COUNT; ++j) {
        acc += fsm->transition[curr][j];
        if(r <= acc) {
            next = j;
            break;
        }
    }
    MoveFSMUpdateCounts(fsm, curr, next);
    fsm->previous = fsm->current;
    fsm->current = static_cast<MoveState>(next);
    return fsm->current;
}

void BotUpdateState(bot_t *pBot) {
    if(!pBot) return;
    BotState next = BotFSMNextState(&pBot->fsm);
    pBot->mission = static_cast<int>(next);
}

void BotUpdateMovement(bot_t *pBot) {
    if(!pBot) return;
    MoveState next = MoveFSMNextState(&pBot->moveFsm);

    if(pBot->pEdict->v.health < 40 && pBot->enemy.ptr)
        next = MOVE_HEAL;
    else if(pBot->enemy.ptr) {
        Vector diff = pBot->enemy.ptr->v.origin - pBot->pEdict->v.origin;
        if(diff.Length() < 80.0f)
            next = MOVE_STAB;
    }
    switch(next) {
        case MOVE_HEAL:
            pBot->strafe_mod = STRAFE_MOD_HEAL;
            break;
        case MOVE_STAB:
            pBot->strafe_mod = STRAFE_MOD_STAB;
            break;
        default:
            pBot->strafe_mod = STRAFE_MOD_NORMAL;
            break;
    }
}

static void JobFSMUpdateCounts(JobFSM *fsm, int from, int to) {
    ++fsm->counts[from][to];
    ++gJobCounts[from][to];
    float total = 0.0f;
    for(int j = 0; j < JOB_TYPE_TOTAL; ++j)
        total += fsm->counts[from][j];
    for(int j = 0; j < JOB_TYPE_TOTAL; ++j)
        fsm->transition[from][j] = (float)fsm->counts[from][j] / total;
}

void JobFSMInit(JobFSM *fsm, int initial) {
    fsm->current = fsm->previous = initial;
    for(int i = 0; i < JOB_TYPE_TOTAL; ++i) {
        unsigned total = 0;
        for(int j = 0; j < JOB_TYPE_TOTAL; ++j) {
            fsm->counts[i][j] = gJobCounts[i][j] ? gJobCounts[i][j] : 1;
            total += fsm->counts[i][j];
        }
        for(int j = 0; j < JOB_TYPE_TOTAL; ++j)
            fsm->transition[i][j] = (float)fsm->counts[i][j] / (float)total;
    }
}

int JobFSMNextState(JobFSM *fsm) {
    float r = random_float(0.0f, 1.0f);
    float acc = 0.0f;
    int curr = fsm->current;
    int next = curr;
    for(int j = 0; j < JOB_TYPE_TOTAL; ++j) {
        acc += fsm->transition[curr][j];
        if(r <= acc) {
            next = j;
            break;
        }
    }
    JobFSMUpdateCounts(fsm, curr, next);
    fsm->previous = fsm->current;
    fsm->current = next;
    return fsm->current;
}

static void WeaponFSMUpdateCounts(WeaponFSM *fsm, int from, int to) {
    ++fsm->counts[from][to];
    ++gWeaponCounts[from][to];
    float total = 0.0f;
    for(int j = 0; j < WEAPON_STATE_COUNT; ++j)
        total += fsm->counts[from][j];
    for(int j = 0; j < WEAPON_STATE_COUNT; ++j)
        fsm->transition[from][j] = (float)fsm->counts[from][j] / total;
}

void WeaponFSMInit(WeaponFSM *fsm, WeaponState initial) {
    fsm->current = fsm->previous = initial;
    bool uniform = true;
    for(int i = 0; i < WEAPON_STATE_COUNT && uniform; ++i)
        for(int j = 0; j < WEAPON_STATE_COUNT && uniform; ++j)
            if(gWeaponCounts[i][j] != 1)
                uniform = false;

    static const unsigned defaults[WEAPON_STATE_COUNT][WEAPON_STATE_COUNT] = {
        {7, 2, 1, 1},  // from PRIMARY
        {4, 5, 1, 1},  // from SECONDARY
        {6, 1, 3, 0},  // from MELEE
        {7, 1, 1, 1}   // from GRENADE
    };

    for(int i = 0; i < WEAPON_STATE_COUNT; ++i) {
        unsigned total = 0;
        for(int j = 0; j < WEAPON_STATE_COUNT; ++j) {
            fsm->counts[i][j] = uniform ? defaults[i][j]
                                       : (gWeaponCounts[i][j] ? gWeaponCounts[i][j] : 1);
            total += fsm->counts[i][j];
        }
        for(int j = 0; j < WEAPON_STATE_COUNT; ++j)
            fsm->transition[i][j] = static_cast<float>(fsm->counts[i][j]) / (float)total;
    }
}

WeaponState WeaponFSMNextState(WeaponFSM *fsm) {
    float r = random_float(0.0f, 1.0f);
    float acc = 0.0f;
    int curr = static_cast<int>(fsm->current);
    int next = curr;
    for(int j = 0; j < WEAPON_STATE_COUNT; ++j) {
        acc += fsm->transition[curr][j];
        if(r <= acc) {
            next = j;
            break;
        }
    }
    WeaponFSMUpdateCounts(fsm, curr, next);
    fsm->previous = fsm->current;
    fsm->current = static_cast<WeaponState>(next);
    return fsm->current;
}

static void ChatFSMUpdateCounts(ChatFSM *fsm, int from, int to) {
    ++fsm->counts[from][to];
    ++gChatCounts[from][to];
    float total = 0.0f;
    for(int j = 0; j < CHAT_STATE_COUNT; ++j)
        total += fsm->counts[from][j];
    for(int j = 0; j < CHAT_STATE_COUNT; ++j)
        fsm->transition[from][j] = (float)fsm->counts[from][j] / total;
}

void ChatFSMInit(ChatFSM *fsm, ChatState initial) {
    fsm->current = fsm->previous = initial;
    for(int i = 0; i < CHAT_STATE_COUNT; ++i) {
        unsigned total = 0;
        for(int j = 0; j < CHAT_STATE_COUNT; ++j) {
            fsm->counts[i][j] = gChatCounts[i][j] ? gChatCounts[i][j] : 1;
            total += fsm->counts[i][j];
        }
        for(int j = 0; j < CHAT_STATE_COUNT; ++j)
            fsm->transition[i][j] = (float)fsm->counts[i][j] / (float)total;
    }
}

ChatState ChatFSMNextState(ChatFSM *fsm) {
    float r = random_float(0.0f, 1.0f);
    float acc = 0.0f;
    int curr = static_cast<int>(fsm->current);
    int next = curr;
    for(int j = 0; j < CHAT_STATE_COUNT; ++j) {
        acc += fsm->transition[curr][j];
        if(r <= acc) {
            next = j;
            break;
        }
    }
    ChatFSMUpdateCounts(fsm, curr, next);
    fsm->previous = fsm->current;
    fsm->current = static_cast<ChatState>(next);
    return fsm->current;
}

static void CombatFSMUpdateCounts(CombatFSM *fsm, int from, int to) {
    ++fsm->counts[from][to];
    ++gCombatCounts[from][to];
    float total = 0.0f;
    for(int j = 0; j < COMBAT_STATE_COUNT; ++j)
        total += fsm->counts[from][j];
    for(int j = 0; j < COMBAT_STATE_COUNT; ++j)
        fsm->transition[from][j] = (float)fsm->counts[from][j] / total;
}

void CombatFSMInit(CombatFSM *fsm, CombatState initial) {
    fsm->current = fsm->previous = initial;
    for(int i = 0; i < COMBAT_STATE_COUNT; ++i) {
        unsigned total = 0;
        for(int j = 0; j < COMBAT_STATE_COUNT; ++j) {
            fsm->counts[i][j] = gCombatCounts[i][j] ? gCombatCounts[i][j] : 1;
            total += fsm->counts[i][j];
        }
        for(int j = 0; j < COMBAT_STATE_COUNT; ++j)
            fsm->transition[i][j] = (float)fsm->counts[i][j] / (float)total;
    }
}

CombatState CombatFSMNextState(CombatFSM *fsm) {
    float r = random_float(0.0f, 1.0f);
    float acc = 0.0f;
    int curr = static_cast<int>(fsm->current);
    int next = curr;
    for(int j = 0; j < COMBAT_STATE_COUNT; ++j) {
        acc += fsm->transition[curr][j];
        if(r <= acc) {
            next = j;
            break;
        }
    }
    CombatFSMUpdateCounts(fsm, curr, next);
    fsm->previous = fsm->current;
    fsm->current = static_cast<CombatState>(next);
    return fsm->current;
}

static void AimFSMUpdateCounts(AimFSM *fsm, int from, int to) {
    ++fsm->counts[from][to];
    ++gAimCounts[from][to];
    float total = 0.0f;
    for(int j = 0; j < AIM_STATE_COUNT; ++j)
        total += fsm->counts[from][j];
    for(int j = 0; j < AIM_STATE_COUNT; ++j)
        fsm->transition[from][j] = (float)fsm->counts[from][j] / total;
}

void AimFSMInit(AimFSM *fsm, AimState initial) {
    fsm->current = fsm->previous = initial;
    for(int i = 0; i < AIM_STATE_COUNT; ++i) {
        unsigned total = 0;
        for(int j = 0; j < AIM_STATE_COUNT; ++j) {
            fsm->counts[i][j] = gAimCounts[i][j] ? gAimCounts[i][j] : 1;
            total += fsm->counts[i][j];
        }
        for(int j = 0; j < AIM_STATE_COUNT; ++j)
            fsm->transition[i][j] = (float)fsm->counts[i][j] / (float)total;
    }
}

AimState AimFSMNextState(AimFSM *fsm) {
    float r = random_float(0.0f, 1.0f);
    float acc = 0.0f;
    int curr = static_cast<int>(fsm->current);
    int next = curr;
    for(int j = 0; j < AIM_STATE_COUNT; ++j) {
        acc += fsm->transition[curr][j];
        if(r <= acc) {
            next = j;
            break;
        }
    }
    AimFSMUpdateCounts(fsm, curr, next);
    fsm->previous = fsm->current;
    fsm->current = (AimState)next;
    return fsm->current;
}

void BotUpdateJob(bot_t *pBot) {
    if(!pBot) return;
    int next = JobFSMNextState(&pBot->jobFsm);
    job_struct *newJob = InitialiseNewJob(pBot, next, true);
    if(newJob)
        SubmitNewJob(pBot, next, newJob);
}

void BotUpdateWeapon(bot_t *pBot) {
    if(!pBot) return;

    WeaponState next = WeaponFSMNextState(&pBot->weaponFsm);

    if(pBot->enemy.ptr) {
        const Vector diff = pBot->enemy.ptr->v.origin - pBot->pEdict->v.origin;
        const float dist = diff.Length();

        if(dist < 70.0f)
            next = WEAPON_MELEE;
        else if(dist > 800.0f && pBot->current_weapon.iAmmo1 > 0)
            next = WEAPON_PRIMARY;
        else if(pBot->current_weapon.iAmmo2 > 0)
            next = WEAPON_SECONDARY;

        if(random_long(0, 50) == 0)
            next = WEAPON_GRENADE;
    }

    if(pBot->desired_combat_state == COMBAT_RETREAT && pBot->current_weapon.iAmmo2 > 0) {
        if(pBot->visEnemyCount > pBot->visAllyCount && random_long(0,100) < 40)
            next = WEAPON_GRENADE;
        else
            next = WEAPON_SECONDARY;
    }

    pBot->desired_weapon_state = static_cast<int>(next);
}

void BotUpdateChat(bot_t *pBot) {
    if(!pBot || pBot->current_wp < 0) return;

    ChatState next = ChatFSMNextState(&pBot->chatFsm);

    job_struct *newJob = InitialiseNewJob(pBot, JOB_CHAT);
    if(!newJob) return;

    switch(next) {
        case CHAT_GREET:
            if(!pBot->greeting && pBot->create_time + 3.0 > pBot->f_think_time &&
               random_long(1, 1000) < bot_chat) {
                MarkovGenerate(newJob->message, MAX_CHAT_LENGTH);
                newJob->message[MAX_CHAT_LENGTH-1] = '\0';
                SubmitNewJob(pBot, JOB_CHAT, newJob);
                pBot->greeting = true;
            }
            break;
        case CHAT_KILL:
            if(pBot->killed_edict && random_long(1, 1000) < bot_chat) {
                MarkovGenerate(newJob->message, MAX_CHAT_LENGTH);
                size_t len = strlen(newJob->message);
                snprintf(newJob->message+len, MAX_CHAT_LENGTH-len, " %s", STRING(pBot->killed_edict->v.netname));
                newJob->message[MAX_CHAT_LENGTH-1] = '\0';
                SubmitNewJob(pBot, JOB_CHAT, newJob);
                pBot->killed_edict = NULL;
            }
            break;
        case CHAT_DEATH:
            if(pBot->killer_edict && random_long(1, 1000) < bot_chat) {
                MarkovGenerate(newJob->message, MAX_CHAT_LENGTH);
                size_t len = strlen(newJob->message);
                snprintf(newJob->message+len, MAX_CHAT_LENGTH-len, " %s", STRING(pBot->killer_edict->v.netname));
                newJob->message[MAX_CHAT_LENGTH-1] = '\0';
                SubmitNewJob(pBot, JOB_CHAT, newJob);
                pBot->killer_edict = NULL;
            }
            break;
        default:
            break;
    }
}

void BotUpdateCombat(bot_t *pBot) {
    if(!pBot) return;
    BotRecordNearbyBots(pBot);
    TeamSignalType sig = BotCurrentSignal(pBot->current_team);

    CombatState next = CombatFSMNextState(&pBot->combatFsm);

    if(pBot->enemy.ptr) {
        Vector diff = pBot->enemy.ptr->v.origin - pBot->pEdict->v.origin;
        const float dist = diff.Length();

        const int oppIdx = ENTINDEX(pBot->enemy.ptr) - 1;
        if(oppIdx >= 0 && oppIdx < MAX_OPPONENTS) {
            int fav = OpponentFavoriteWeapon(pBot->opponents[oppIdx]);
            if(fav == TF_WEAPON_SNIPERRIFLE && dist > 300.0f)
                next = COMBAT_APPROACH;
        }

        if(pBot->pEdict->v.health < 25 || pBot->desired_reaction_state == REACT_PANIC)
            next = COMBAT_RETREAT;
        else if(PlayerHealthPercent(pBot->pEdict) < 50 &&
                pBot->visEnemyCount > pBot->visAllyCount)
            next = COMBAT_COVER;
        else if(pBot->desired_reaction_state == REACT_ALERT)
            next = COMBAT_ATTACK;
        else if(dist < 200.0f)
            next = COMBAT_ATTACK;
        else if(dist > 600.0f)
            next = COMBAT_APPROACH;
        else if(dist > 300.0f && random_long(0,100) < 30)
            next = COMBAT_FLANK;

        if(PlayerHealthPercent(pBot->pEdict) < 30 &&
           pBot->visEnemyCount > pBot->visAllyCount)
            next = COMBAT_RETREAT;
        else if(PlayerHealthPercent(pBot->pEdict) > 60 &&
                pBot->visAllyCount > pBot->visEnemyCount + 1)
            next = COMBAT_APPROACH;
    }
    else {
        edict_t *shared = BotGetSharedEnemy(pBot);
        if(shared) {
            pBot->enemy.ptr = shared;
            next = COMBAT_APPROACH;
        }
    }

    if(sig == SIG_ATTACK)
        next = COMBAT_ATTACK;

    if(next == COMBAT_ATTACK && pBot->enemy.ptr)
        BotBroadcastSignal(pBot->current_team, SIG_ATTACK, 0.5f);

    pBot->desired_combat_state = static_cast<int>(next);
}

void BotUpdateAim(bot_t *pBot) {
    if(!pBot) return;
    AimState next = AimFSMNextState(&pBot->aimFsm);

    if(pBot->enemy.ptr && pBot->desired_combat_state == COMBAT_ATTACK) {
        if(pBot->enemy.ptr->v.health < 30)
            next = AIM_HEAD;
        else
            next = AIM_BODY;
    }

    pBot->desired_aim_state = (int)next;
}

void BotApplyCombatState(bot_t *pBot) {
    if(!pBot || !pBot->enemy.ptr)
        return;
    switch(static_cast<CombatState>(pBot->desired_combat_state)) {
        case COMBAT_APPROACH: {
            job_struct *newJob = InitialiseNewJob(pBot, JOB_PURSUE_ENEMY);
            if(newJob) {
                newJob->player = pBot->enemy.ptr;
                newJob->origin = pBot->enemy.ptr->v.origin;
                SubmitNewJob(pBot, JOB_PURSUE_ENEMY, newJob);
            }
            break;
        }
        case COMBAT_RETREAT: {
            int retreat = BotFindRetreatPoint(pBot, 800, pBot->enemy.ptr->v.origin);
            if(retreat != -1)
                pBot->goto_wp = retreat;
            break;
        }
        case COMBAT_FLANK: {
            BotFlankEnemy(pBot);
            job_struct *newJob = InitialiseNewJob(pBot, JOB_PURSUE_ENEMY);
            if(newJob) {
                newJob->player = pBot->enemy.ptr;
                newJob->origin = pBot->enemy.ptr->v.origin;
                SubmitNewJob(pBot, JOB_PURSUE_ENEMY, newJob);
            }
            break;
        }
        case COMBAT_COVER: {
            BotSeekCover(pBot);
            break;
        }
        case COMBAT_ATTACK:
        case COMBAT_IDLE:
        default:
            break;
    }
}

static void NavFSMUpdateCounts(NavFSM *fsm, int from, int to) {
    ++fsm->counts[from][to];
    ++gNavCounts[from][to];
    float total = 0.0f;
    for(int j = 0; j < NAV_STATE_COUNT; ++j)
        total += fsm->counts[from][j];
    for(int j = 0; j < NAV_STATE_COUNT; ++j)
        fsm->transition[from][j] = (float)fsm->counts[from][j] / total;
}

void NavFSMInit(NavFSM *fsm, NavState initial) {
    fsm->current = fsm->previous = initial;
    for(int i = 0; i < NAV_STATE_COUNT; ++i) {
        unsigned total = 0;
        for(int j = 0; j < NAV_STATE_COUNT; ++j) {
            fsm->counts[i][j] = gNavCounts[i][j] ? gNavCounts[i][j] : 1;
            total += fsm->counts[i][j];
        }
        for(int j = 0; j < NAV_STATE_COUNT; ++j)
            fsm->transition[i][j] = (float)fsm->counts[i][j] / (float)total;
    }
}

NavState NavFSMNextState(NavFSM *fsm) {
    float r = random_float(0.0f, 1.0f);
    float acc = 0.0f;
    int curr = static_cast<int>(fsm->current);
    int next = curr;
    for(int j = 0; j < NAV_STATE_COUNT; ++j) {
        acc += fsm->transition[curr][j];
        if(r <= acc) {
            next = j;
            break;
        }
    }
    NavFSMUpdateCounts(fsm, curr, next);
    fsm->previous = fsm->current;
    fsm->current = static_cast<NavState>(next);
    return fsm->current;
}

void BotUpdateNavigation(bot_t *pBot) {
    if(!pBot) return;
    BotRecordNearbyBots(pBot);
    TeamSignalType sig = BotCurrentSignal(pBot->current_team);
    NavState next = NavFSMNextState(&pBot->navFsm);

    if(pBot->enemy.ptr) {
        const int oppIdx = ENTINDEX(pBot->enemy.ptr) - 1;
        if(oppIdx >= 0 && oppIdx < MAX_OPPONENTS) {
            int favWp = OpponentFavoredWaypoint(pBot->opponents[oppIdx]);
            if(favWp == pBot->current_wp)
                next = NAV_STRAFE;
        }
    }
    else {
        edict_t *shared = BotGetSharedEnemy(pBot);
        if(shared)
            pBot->enemy.ptr = shared;
    }

    if(sig == SIG_ATTACK && pBot->enemy.ptr)
        next = NAV_STRAFE;
    else if(pBot->desired_reaction_state == REACT_PANIC)
        next = (random_float(0.0f, 1.0f) < 0.5f) ? NAV_STRAFE : NAV_JUMP;
    pBot->desired_nav_state = static_cast<int>(next);
}

void BotApplyNavState(bot_t *pBot) {
    if(!pBot) return;
    switch(static_cast<NavState>(pBot->desired_nav_state)) {
        case NAV_STRAFE:
            pBot->f_waypoint_drift = random_long(0,1) ? 10.0f : -10.0f;
            pBot->f_side_speed = (random_long(0,1) ? pBot->f_max_speed : -pBot->f_max_speed);
            if(pBot->desired_reaction_state == REACT_PANIC && random_long(0,1))
                pBot->pEdict->v.button |= IN_JUMP;
            break;
        case NAV_JUMP:
            pBot->pEdict->v.button |= IN_JUMP;
            if(pBot->desired_reaction_state == REACT_PANIC) {
                pBot->f_side_speed = (random_long(0,1) ? pBot->f_max_speed : -pBot->f_max_speed);
                pBot->f_waypoint_drift = random_long(0,1) ? 10.0f : -10.0f;
            } else {
                pBot->f_waypoint_drift = 0.0f;
            }
            break;
        case NAV_STRAIGHT:
        default:
            pBot->f_waypoint_drift = 0.0f;
            break;
    }
}

static void ReactionFSMUpdateCounts(ReactionFSM *fsm, int from, int to) {
    ++fsm->counts[from][to];
    ++gReactionCounts[from][to];
    float total = 0.0f;
    for(int j=0;j<REACT_STATE_COUNT;++j)
        total += fsm->counts[from][j];
    for(int j=0;j<REACT_STATE_COUNT;++j)
        fsm->transition[from][j] = (float)fsm->counts[from][j] / total;
}

void ReactionFSMInit(ReactionFSM *fsm, ReactionState initial) {
    fsm->current = fsm->previous = initial;
    for(int i=0;i<REACT_STATE_COUNT;i++) {
        unsigned total = 0;
        for(int j=0;j<REACT_STATE_COUNT;j++) {
            fsm->counts[i][j] = gReactionCounts[i][j] ? gReactionCounts[i][j] : 1;
            total += fsm->counts[i][j];
        }
        for(int j=0;j<REACT_STATE_COUNT;j++)
            fsm->transition[i][j] = (float)fsm->counts[i][j] / (float)total;
    }
}

ReactionState ReactionFSMNextState(ReactionFSM *fsm, float avgAllyHealth, int recentKills) {
    float weights[REACT_STATE_COUNT];
    float total = 0.0f;
    int curr = static_cast<int>(fsm->current);

    for(int j = 0; j < REACT_STATE_COUNT; ++j)
        weights[j] = fsm->transition[curr][j];

    if(avgAllyHealth < 50.0f)
        weights[REACT_PANIC] += 0.25f;
    if(recentKills > 0)
        weights[REACT_CALM] += 0.15f * recentKills;

    for(int j = 0; j < REACT_STATE_COUNT; ++j)
        total += weights[j];

    float r = random_float(0.0f, total);
    float acc = 0.0f;
    int next = curr;
    for(int j = 0; j < REACT_STATE_COUNT; ++j) {
        acc += weights[j];
        if(r <= acc) { next = j; break; }
    }

    ReactionFSMUpdateCounts(fsm, curr, next);
    fsm->previous = fsm->current;
    fsm->current = static_cast<ReactionState>(next);
    return fsm->current;
}

static float g_fsm_next_save = 0.0f;

void FSMPeriodicSave(float currentTime) {
    if(currentTime >= g_fsm_next_save) {
        SaveFSMCounts();
        SaveBotMetrics();
        float interval = CVAR_GET_FLOAT("bot_save_interval");
        if(interval <= 0.0f)
            interval = 120.0f;
        g_fsm_next_save = currentTime + interval;
    }
}

void BotUpdateReaction(bot_t *pBot) {
    if(!pBot) return;
    float allyHealth = GetAverageAllyHealth(pBot);
    int teamKills = CountRecentAllyKills(pBot);
    ReactionState next = ReactionFSMNextState(&pBot->reactFsm, allyHealth, teamKills);

    if(pBot->pEdict->v.health < 20)
        next = REACT_PANIC;
    else if(pBot->enemy.ptr)
        next = REACT_ALERT;
    else if(pBot->desired_reaction_state == REACT_ALERT && !pBot->enemy.ptr)
        next = REACT_CALM;

    if(PlayerHealthPercent(pBot->pEdict) < 40 &&
       pBot->visEnemyCount > pBot->visAllyCount + 1)
        next = REACT_PANIC;

    pBot->desired_reaction_state = static_cast<int>(next);
}

void BotRecordNearbyBots(bot_t *pBot) {
    if(!pBot) return;
    pBot->nearbyBotCount = 0;
    for(int i=0;i<MAX_BOTS && pBot->nearbyBotCount < MAX_NEARBY_BOTS;i++) {
        bot_t *other = &bots[i];
        if(!other->is_used || other == pBot)
            continue;
        if(other->current_team != pBot->current_team)
            continue;
        if((other->pEdict->v.origin - pBot->pEdict->v.origin).Length() > 600.0f)
            continue;
        NearbyBotState &info = pBot->nearbyBots[pBot->nearbyBotCount++];
        info.bot = other->pEdict;
        info.enemy = other->enemy.ptr;
        info.combat_state = other->desired_combat_state;
        info.nav_state = other->desired_nav_state;
        info.last_update = pBot->f_think_time;
    }
}

edict_t *BotGetSharedEnemy(const bot_t *pBot) {
    int bestCount = 0;
    edict_t *best = nullptr;
    for(int i=0;i<pBot->nearbyBotCount;i++) {
        edict_t *enemy = pBot->nearbyBots[i].enemy;
        if(!enemy) continue;
        int count = 1;
        for(int j=i+1;j<pBot->nearbyBotCount;j++) {
            if(pBot->nearbyBots[j].enemy == enemy)
                ++count;
        }
        if(count > bestCount) {
            bestCount = count;
            best = enemy;
        }
    }
    if(bestCount > 1)
        return best;
    return nullptr;
}

void BotBroadcastSignal(int team, TeamSignalType signal, float duration) {
    if(team < 0 || team >= MAX_TEAMS) return;
    g_teamSignals[team] = signal;
    g_teamSignalExpire[team] = gpGlobals->time + duration;
}

TeamSignalType BotCurrentSignal(int team) {
    if(team < 0 || team >= MAX_TEAMS) return SIG_NONE;
    if(gpGlobals->time > g_teamSignalExpire[team])
        g_teamSignals[team] = SIG_NONE;
    return g_teamSignals[team];
}
