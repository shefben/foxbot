#include "bot_markov.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <sstream>
#include <deque>
#include <fstream>

static std::unordered_map<std::string, std::vector<std::string>> g_chain;
static size_t g_order = 3; // default to trigram

void MarkovInit() {
    g_chain.clear();
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
    if(!out) return false;
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
    if(!in) return false;
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
