#include "FSManager.h"

/**
 * @brief Initializes the file system.
 * 
 * This function attempts to initialize the LittleFS file system. 
 * If the initialization fails, it returns false.
 * 
 * @return true if the file system was successfully initialized, false otherwise.
 */
void FSManager::init() {
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: Failed to initialize LittleFS");
    } else {
        Serial.println("STATUS: LittleFS initialized successfully");
    }
}

/**
 * @brief Lists all files in the LittleFS filesystem.
 * 
 * This function opens the root directory of the LittleFS filesystem and iterates
 * through all the files, constructing a JSON-formatted string that contains the
 * name and size of each file.
 * 
 * @return A JSON-formatted string representing the list of files in the filesystem.
 *         Each file is represented as an object with "name" and "size" properties.
 */
String FSManager::listFiles() {
    String output = "[";
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    
    while(file) {
        if (output != "[") {
            output += ",";
        }
        output += "{\"name\":\"" + String(file.name()) + "\",";
        output += "\"size\":\"" + String(file.size()) + "\"}";
        file = root.openNextFile();
    }
    output += "]";
    return output;
}

/**
 * @brief Reads the content of a file from the LittleFS filesystem.
 * 
 * This method attempts to open a file at the specified path in read mode.
 * If the file exists and is successfully opened, its content is read into
 * a String and returned. If the file does not exist or cannot be opened,
 * an empty String is returned.
 * 
 * @param path The path to the file to be read.
 * @return A String containing the content of the file, or an empty String
 *         if the file does not exist or cannot be opened.
 */
String FSManager::readFile(const String& path) {
    if (!LittleFS.exists(path)) {
        return "";
    }
    
    File file = LittleFS.open(path, "r");
    if (!file) {
        return "";
    }
    
    String content = file.readString();
    file.close();
    return content;
}