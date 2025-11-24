//
// Created by Haoran Gan on 11/23/25.
//

#include "common/crash_point.hpp"

#include <cstring>
#include <unistd.h>

namespace {
bool g_armed = false;
std::string g_name;
int g_remaining = 0;
}  // namespace

void ArmCrashPoint(const std::string &name, int nth) {
    g_armed = true;
    g_name = name;
    g_remaining = nth;
}

bool CrashPointHit(const char *name) {
    if (!g_armed || std::strcmp(name, g_name.c_str()) != 0) {
        return false;
    }
    return --g_remaining == 0;
}

void CrashNow() {
    _exit(99);
}
