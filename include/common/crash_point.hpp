//
// Created by Haoran Gan on 11/23/25.
//

#pragma once

#include <string>

// Test-only fault injection. Code calls CRASH_POINT("name") at interesting
// moments; a test arms one name, and the nth time it is reached the process
// dies immediately (like SIGKILL: no destructors, nothing flushed).

void ArmCrashPoint(const std::string &name, int nth);

// True if the armed point is reached for the nth time right now. Callers that
// want to simulate a partial write check this first, then call CrashNow().
bool CrashPointHit(const char *name);

[[noreturn]] void CrashNow();

#define CRASH_POINT(name)                \
    do {                                 \
        if (CrashPointHit(name)) {       \
            CrashNow();                  \
        }                                \
    } while (0)
