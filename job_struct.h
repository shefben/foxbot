#ifndef JOB_STRUCT_H
#define JOB_STRUCT_H

#include "tf_defs.h" // for edict_t and Vector
#include "osdep.h"
#include "compat.h"

// maximum number of jobs storable in the bot's job buffer
#define JOB_BUFFER_MAX 5

struct job_struct {
    float f_bufferedTime; // how long ago this job was put into the buffer
    int priority;         // how important this job is currently

    int phase;         // internal job state, should be zero when job is created
    float phase_timer; // used in various ways by each job type

    int waypoint;                  // waypoint this job concerns
    int waypointTwo;               // a few jobs involve two waypoints
    edict_t *object;               // object this job concerns
    edict_t *player;               // player this job concerns
    Vector origin;                 // map coordinates this job concerns
    char message[MAX_CHAT_LENGTH]; // used only by jobs that involve talking
};

// maximum number of jobs that can be blacklisted simultaneously
#define JOB_BLACKLIST_MAX 5

struct job_blacklist_struct {
    int type;        // type of job being blacklisted
    float f_timeOut; // time when job should be allowed back into the job buffer
};

#endif // JOB_STRUCT_H
