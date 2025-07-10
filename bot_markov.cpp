#include "bot_markov.h"
#include <map>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cctype>

static std::map<std::string, std::vector<std::string> > g_chain;

void MarkovInit() {
    g_chain.clear();
}

static void add_pair(const std::string &w1, const std::string &w2) {
    g_chain[w1].push_back(w2);
}

void MarkovAddSentence(const char *line) {
    if(!line) return;
    std::string prev;
    char word[32];
    const char *p = line;
    int n;
    while(sscanf(p, "%31s%n", word, &n) == 1) {
        for(int i=0; word[i]; ++i) word[i] = static_cast<char>(tolower(word[i]));
        if(!prev.empty())
            add_pair(prev, word);
        prev = word;
        p += n;
        while(*p && isspace(*p)) ++p;
    }
}

void MarkovGenerate(char *out, size_t maxLen) {
    if(!out || maxLen==0) return;
    if(g_chain.empty()) {
        out[0] = '\0';
        return;
    }
    size_t index = rand() % g_chain.size();
    std::map<std::string, std::vector<std::string> >::iterator it = g_chain.begin();
    for(size_t i=0; i<index; ++i) ++it;
    std::string word = it->first;
    size_t len = 0;
    out[0] = '\0';
    for(int step=0; step<12 && len + word.length() + 1 < maxLen; ++step) {
        if(len) {
            out[len++] = ' ';
        }
        std::strncpy(out+len, word.c_str(), maxLen - len - 1);
        len += word.length();
        out[len] = '\0';
        const std::vector<std::string> &next = g_chain[word];
        if(next.empty())
            break;
        word = next[rand() % next.size()];
    }
    out[len] = '\0';
}

bool MarkovSave(const char *file) {
    if(!file) return false;
    FILE *fp = fopen(file, "w");
    if(!fp) return false;
    for(std::map<std::string, std::vector<std::string> >::iterator it = g_chain.begin(); it != g_chain.end(); ++it) {
        fprintf(fp, "%s %u", it->first.c_str(), (unsigned)it->second.size());
        for(size_t i=0; i<it->second.size(); ++i)
            fprintf(fp, " %s", it->second[i].c_str());
        fprintf(fp, "\n");
    }
    fclose(fp);
    return true;
}

bool MarkovLoad(const char *file) {
    if(!file) return false;
    FILE *fp = fopen(file, "r");
    if(!fp) return false;
    g_chain.clear();
    char line[512];
    while(fgets(line, sizeof(line), fp)) {
        char *tok = strtok(line, " \t\n");
        if(!tok) continue;
        std::string first = tok;
        tok = strtok(NULL, " \t\n");
        if(!tok) continue;
        int count = atoi(tok);
        for(int i=0;i<count;i++) {
            tok = strtok(NULL, " \t\n");
            if(!tok) break;
            add_pair(first, tok);
            first = tok;
        }
    }
    fclose(fp);
    return true;
}

static float g_next_save_time = 0.0f;

void MarkovPeriodicSave(const char *file, float currentTime) {
    if(currentTime >= g_next_save_time) {
        if(file)
            MarkovSave(file);
        g_next_save_time = currentTime + 120.0f;
    }
}
