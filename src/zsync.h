#ifndef ZSYNC_H
#define ZSYNC_H
#include <string>
#include <vector>
class ZObject;class ZMap;
// Only authoritative replicated fields; excludes interpolation and local effects.
std::string ZSync_Hash(const std::vector<ZObject*> &objects,ZMap &map);
#endif
