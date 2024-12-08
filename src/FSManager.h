#pragma once
#include <LittleFS.h>

class FSManager {
public:
    static void init();
    static String listFiles();
    static String readFile(const String& path);
};