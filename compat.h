#ifndef FOXBOT_COMPAT_H
#define FOXBOT_COMPAT_H

#if defined(_MSC_VER) && _MSC_VER <= 1200
#ifndef nullptr
#define nullptr NULL
#endif
#ifndef constexpr
#define constexpr const
#endif
#ifndef noexcept
#define noexcept
#endif

#include <map>
#include <set>
#define unordered_map map
#define unordered_set set
#endif

#endif // FOXBOT_COMPAT_H
