#include "AITagService.h"
#include "json.hpp"
#include <sstream>
#include <algorithm>

TagResult AITagService::tagFile(int64_t fileId, const std::string& content, const std::string& fileName) {
    TagResult r;
    if (!isAIEnabled()) {
        r.success = true;
        r.tags.push_back("WeiFenLei");  // "未分类" fallback tag
        return r;
    }

    // Detect file type from extension
    std::string ext;
    auto pos = fileName.rfind('.');
    if (pos != std::string::npos) ext = fileName.substr(pos + 1);

    std::string systemPrompt =
        "You are a file classifier. Analyze the file and suggest 3-5 tags. "
        "Return EXACTLY this JSON: {\"tags\":[\"tag1\",\"tag2\"],\"newTagSuggestions\":[]}";

    std::string userContent =
        "File name: " + fileName + "\nExtension: " + ext + "\n"
        "Content (first 2000 chars):\n" + content.substr(0, 2000);

    APIBridge::AIResponse aiResp = api()->chat(systemPrompt, userContent);

    if (aiResp.success) {
        return parseTagResponse(aiResp.content);
    }

    r.success = true;
    r.tags.push_back("WeiFenLei");
    return r;
}

void AITagService::tagFileAsync(int64_t fileId, const std::string& content, const std::string& fileName, TagCallback cb) {
    if (!isAIEnabled()) {
        TagResult r; r.success = true; r.tags.push_back("WeiFenLei");
        if (cb) cb(r);
        return;
    }

    std::string systemPrompt =
        "You are a file classifier. Return: {\"tags\":[\"...\",\"...\"],\"newTagSuggestions\":[]}";

    std::string userContent = "File: " + fileName + "\nContent: " + content.substr(0, 2000);

    api()->chatAsync(systemPrompt, userContent, [cb, this](APIBridge::AIResponse aiResp) {
        TagResult r;
        if (aiResp.success) {
            r = parseTagResponse(aiResp.content);
        } else {
            r.success = false; r.errorMsg = aiResp.errorMsg;
        }
        if (cb) cb(r);
    });
}

TagResult AITagService::parseTagResponse(const std::string& jsonContent) {
    TagResult r;
    try {
        auto v = aijson::Value::parse(jsonContent);
        r.success = true;
        // Parse tags array
        auto& tagsVal = v["tags"];
        if (tagsVal.type == aijson::Value::Array) {
            for (size_t i = 0; i < tagsVal.arrVal.size(); i++) {
                if (tagsVal.arrVal[i].type == aijson::Value::String) {
                    r.tags.push_back(tagsVal.arrVal[i].strVal);
                }
            }
        }
        auto& newTags = v["newTagSuggestions"];
        if (newTags.type == aijson::Value::Array) {
            for (size_t i = 0; i < newTags.arrVal.size(); i++) {
                if (newTags.arrVal[i].type == aijson::Value::String) {
                    r.newTagSuggestions.push_back(newTags.arrVal[i].strVal);
                }
            }
        }
    } catch (...) {
        r.success = false;
        r.errorMsg = "Failed to parse AI tag response";
    }
    return r;
}
