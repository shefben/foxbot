#ifndef BOT_MARKOV_H
#define BOT_MARKOV_H

#include <cstddef>

void MarkovInit();
void MarkovAddSentence(const char *line);
void MarkovGenerate(char *out, size_t maxLen);
void MarkovGenerateContextual(const char *context, char *out, size_t maxLen);
bool MarkovSave(const char *file);
bool MarkovLoad(const char *file);
void MarkovPeriodicSave(const char *file, float currentTime);

#endif // BOT_MARKOV_H
