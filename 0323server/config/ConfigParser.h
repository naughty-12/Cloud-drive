#ifndef CONFIGPARSER_H
#define CONFIGPARSER_H

#include <string>
#include <cstdlib>

// Simple JSON parser (no external lib — parse config with simple string matching)
// Extracted from NodeManager.cpp for reuse across the server (tcpkernel, NodeManager, etc.)

inline std::string jsonGetString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    // skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.length()) return "";
    if (json[pos] == '"') {
        pos++; // skip opening quote
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }
    return "";
}

inline int jsonGetInt(const std::string& json, const std::string& key) {
    std::string val = jsonGetString(json, key);
    if (val.empty()) {
        // try numeric
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return 0;
        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        std::string num;
        while (pos < json.length() && (isdigit(json[pos]) || json[pos] == '-')) {
            num += json[pos++];
        }
        return num.empty() ? 0 : atoi(num.c_str());
    }
    return atoi(val.c_str());
}

// Extract a nested JSON object by key (e.g. key="ai" → returns the {...} content)
inline std::string jsonGetObject(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    // skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.length() || json[pos] != '{') return "";

    // Find matching closing brace
    int depth = 0;
    size_t start = pos;
    while (pos < json.length()) {
        if (json[pos] == '{') depth++;
        else if (json[pos] == '}') {
            depth--;
            if (depth == 0) {
                return json.substr(start, pos - start + 1);
            }
        }
        pos++;
    }
    return "";
}

#endif // CONFIGPARSER_H
