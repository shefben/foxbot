#include "bot.h"
#include "bot_fsm.h"
#include "bot_job_think.h"
#include "bot_func.h"

// Returns the highest base priority currently stored in the job buffer.
static int PlannerHighestPriority(const bot_t *pBot) {
    int highest = PRIORITY_NONE;
    for(int i = 0; i < JOB_BUFFER_MAX; ++i) {
        if(pBot->jobType[i] != JOB_NONE) {
            int pri = jl[pBot->jobType[i]].basePriority;
            if(pri > highest)
                highest = pri;
        }
    }
    return highest;
}

// Initialize planner related state for the bot.
void PlannerInit(bot_t *pBot) {
    if(!pBot)
        return;
    JobFSMInit(&pBot->jobFsm, JOB_ROAM);
}

// Select and buffer long term goals based on the job FSM and current priorities.
void PlannerThink(bot_t *pBot) {
    if(!pBot)
        return;

    const int next = JobFSMNextState(&pBot->jobFsm);
    const int candidatePriority = jl[next].basePriority;

    if(candidatePriority >= PlannerHighestPriority(pBot)) {
        job_struct *newJob = InitialiseNewJob(pBot, next, true);
        if(newJob)
            SubmitNewJob(pBot, next, newJob);
    }
}


