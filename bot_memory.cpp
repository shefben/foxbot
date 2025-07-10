#include "bot_memory.h"
#include "bot.h"
#include "util.h"
#include <cstdio>
#include <cstring>

static const unsigned MEMORY_FILE_VERSION = 1;

void LoadBotMemory() {
    char fname[256];
    UTIL_BuildFileName(fname, 255, (char*)"bot_memory.dat", NULL);
    FILE *fp = fopen(fname, "rb");
    if(!fp) {
        for(int i=0;i<32;i++) {
            bots[i].killer_name[0] = '\0';
            for(int j=0;j<MAX_OPPONENTS;j++)
                memset(&bots[i].opponents[j], 0, sizeof(OpponentInfo));
        }
        return;
    }
    unsigned version = 0;
    if(fread(&version, sizeof(unsigned), 1, fp) != 1 || version != MEMORY_FILE_VERSION) {
        fclose(fp);
        for(int i=0;i<32;i++) {
            bots[i].killer_name[0] = '\0';
            for(int j=0;j<MAX_OPPONENTS;j++)
                memset(&bots[i].opponents[j], 0, sizeof(OpponentInfo));
        }
        return;
    }
    for(int i=0;i<32;i++) {
        fread(bots[i].killer_name, sizeof(char), BOT_NAME_LEN+1, fp);
        fread(bots[i].opponents, sizeof(OpponentInfo), MAX_OPPONENTS, fp);
        bots[i].killer_edict = nullptr;
        bots[i].killed_edict = nullptr;
    }
    fclose(fp);
}

void SaveBotMemory() {
    char fname[256];
    UTIL_BuildFileName(fname, 255, (char*)"bot_memory.dat", NULL);
    FILE *fp = fopen(fname, "wb");
    if(!fp) return;
    fwrite(&MEMORY_FILE_VERSION, sizeof(unsigned), 1, fp);
    for(int i=0;i<32;i++) {
        fwrite(bots[i].killer_name, sizeof(char), BOT_NAME_LEN+1, fp);
        fwrite(bots[i].opponents, sizeof(OpponentInfo), MAX_OPPONENTS, fp);
    }
    fclose(fp);
}
