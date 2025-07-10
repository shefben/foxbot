#include "bot_markov.h"
#include "bot.h"
#include "bot_memory.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <sstream>
#include <deque>
#include <fstream>
#include <cstdio>
#include <enginecallback.h>
#include "compat.h"

static std::unordered_map<std::string, std::vector<std::string>> g_chain;
static std::unordered_map<std::string, std::vector<std::string>> g_context_lines;
static size_t g_order = 3; // default to trigram

void MarkovInit() {
    g_chain.clear();
    g_context_lines.clear();
    g_order = 3;
}

static void add_pair(const std::string &w1, const std::string &w2) {
    g_chain[w1].push_back(w2);
}

void MarkovAddSentence(const char *line) {
    if(!line) return;
    std::deque<std::string> window;
    char word[32];
    const char *p = line;
    int n;
    while(sscanf(p, "%31s%n", word, &n) == 1) {
        for(int i=0; word[i]; ++i) word[i] = static_cast<char>(tolower(word[i]));
        window.push_back(word);
        if(window.size() == g_order) {
            std::string prefix;
            for(size_t i=0; i+1<window.size(); ++i) {
                if(i) prefix.push_back(' ');
                prefix += window[i];
            }
            add_pair(prefix, window.back());
            window.pop_front();
        }
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
    auto it = g_chain.begin();
    std::advance(it, index);
    std::string prefix = it->first;
    std::vector<std::string> words;
    {
        std::istringstream iss(prefix);
        std::string tok;
        while(iss >> tok) words.push_back(tok);
    }

    size_t len = 0;
    out[0] = '\0';
    for(const auto &w : words) {
        if(len + w.length() + 1 >= maxLen) break;
        if(len) out[len++] = ' ';
        std::strncpy(out+len, w.c_str(), maxLen - len - 1);
        len += w.length();
        out[len] = '\0';
    }

    for(int step=0; step<12; ++step) {
        auto vit = g_chain.find(prefix);
        if(vit == g_chain.end() || vit->second.empty())
            break;
        const std::string &next = vit->second[rand() % vit->second.size()];
        if(len + next.length() + 1 >= maxLen) break;
        if(len) out[len++] = ' ';
        std::strncpy(out+len, next.c_str(), maxLen - len - 1);
        len += next.length();
        out[len] = '\0';

        words.push_back(next);
        if(words.size() > g_order - 1)
            words.erase(words.begin());
        prefix.clear();
        for(size_t i=0; i<words.size(); ++i) {
            if(i) prefix.push_back(' ');
            prefix += words[i];
        }
    }
    out[len] = '\0';
}

bool MarkovSave(const char *file) {
    if(!file) return false;
    std::ofstream out(file);
    if(!out) {
        UTIL_BotLogPrintf("MarkovSave: failed to open %s\n", file);
        return false;
    }
    out << "N " << g_order << '\n';
    for(const auto &it : g_chain) {
        out << it.first << ' ' << it.second.size();
        for(const auto &n : it.second)
            out << ' ' << n;
        out << '\n';
    }
    return true;
}

bool MarkovLoad(const char *file) {
    if(!file) return false;
    std::ifstream in(file);
    if(!in) {
        UTIL_BotLogPrintf("MarkovLoad: failed to open %s\n", file);
        return false;
    }
    g_chain.clear();

    std::string line;
    if(!std::getline(in, line))
        return true;

    std::istringstream hdr(line);
    std::string tok;
    if(hdr >> tok && tok == "N") {
        hdr >> g_order;
    } else {
        g_order = 2; // old format
        std::istringstream iss(line);
        std::string first;
        if(!(iss >> first)) return true;
        int count = 0;
        iss >> count;
        for(int i=0;i<count;i++) {
            std::string w;
            if(!(iss >> w)) break;
            add_pair(first, w);
            first = w;
        }
    }

    while(std::getline(in, line)) {
        if(line.empty()) continue;
        std::istringstream iss(line);
        if(g_order == 2) {
            std::string first;
            int count;
            if(!(iss >> first >> count)) continue;
            for(int i=0;i<count;i++) {
                std::string w;
                if(!(iss >> w)) break;
                add_pair(first, w);
                first = w;
            }
        } else {
            std::vector<std::string> tokens;
            std::string w;
            while(iss >> w) tokens.push_back(w);
            if(tokens.size() < g_order) continue;
            std::string prefix;
            for(size_t i=0;i<g_order-1;i++) {
                if(i) prefix.push_back(' ');
                prefix += tokens[i];
            }
            size_t count = std::stoul(tokens[g_order-1]);
            size_t pos = g_order;
            for(size_t i=0;i<count && pos < tokens.size(); ++i, ++pos)
                add_pair(prefix, tokens[pos]);
        }
    }

    // Seed chain with default chat lines for better vocabulary
    char chatFile[256];
    UTIL_BuildFileName(chatFile, 255, (char*)"foxbot_chat.txt", nullptr);
    FILE *cfp = fopen(chatFile, "r");
    if(cfp) {
        char buf[256];
        std::string current;
        while(UTIL_ReadFileLine(buf, sizeof(buf), cfp)) {
            size_t len = strlen(buf);
            if(len && (buf[len-1] == '\n' || buf[len-1] == '\r'))
                buf[--len] = '\0';
            if(buf[0] == '#' || buf[0] == '\0')
                continue;
            if(buf[0] == '[') {
                char ctx[64];
                strncpy(ctx, buf+1, sizeof(ctx)-1);
                ctx[sizeof(ctx)-1] = '\0';
                char *end = strchr(ctx, ']');
                if(end) *end = '\0';
                for(size_t i=0; ctx[i]; ++i) ctx[i] = static_cast<char>(tolower(ctx[i]));
                current = ctx;
                continue;
            }
            char *ptr = strstr(buf, "%n");
            if(ptr) *(ptr+1) = 's';
            MarkovAddSentence(buf);
            if(!current.empty())
                g_context_lines[current].emplace_back(buf);
        }
        fclose(cfp);
    }

    return true;
}

static float g_next_save_time = 0.0f;

void MarkovGenerateContextual(const char *context, char *out, size_t maxLen) {
    if(!context) {
        MarkovGenerate(out, maxLen);
        return;
    }
    std::string key(context);
    for(char &c : key) c = static_cast<char>(tolower(c));
    auto it = g_context_lines.find(key);
    if(it == g_context_lines.end() || it->second.empty()) {
        MarkovGenerate(out, maxLen);
        return;
    }
    const std::string &phrase = it->second[rand() % it->second.size()];
    strncpy(out, phrase.c_str(), maxLen-1);
    out[maxLen-1] = '\0';
}

void MarkovPeriodicSave(const char *file, float currentTime) {
    if(currentTime >= g_next_save_time) {
        if(file) {
            if(!MarkovSave(file)) {
                UTIL_BotLogPrintf("MarkovPeriodicSave: failed to save %s\n", file);
            } else {
                UTIL_BotLogPrintf("MarkovPeriodicSave: saved %s at %f\n", file, currentTime);
            }
        }
        SaveBotMemory();
        float interval = CVAR_GET_FLOAT("bot_save_interval");
        if(interval <= 0.0f)
            interval = 120.0f;
        g_next_save_time = currentTime + interval;
    }
}
