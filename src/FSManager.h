#pragma once
#include <LittleFS.h>

class FSManager {
public:
    FSManager() = default;
    bool init();
};