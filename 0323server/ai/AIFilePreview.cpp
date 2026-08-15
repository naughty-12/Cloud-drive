#include "AIFilePreview.h"
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

PreviewResult AIFilePreview::preview(int64_t fileId, const std::string& content, const std::string& fileName) {
    PreviewResult r;
    if (!isAIEnabled()) {
        // Fallback: return first 200 chars as summary
        r.success = true;
        r.summary = content.substr(0, std::min<size_t>(200, content.size())) + (content.size() > 200 ? "..." : "");
        r.fileType = "unknown";
        r.keywords = fileName;
        r.keySentences = "";
        r.errorMsg = "AI disabled: set SILICONFLOW_API_KEY, OPENAI_API_KEY, or OLLAMA_HOST environment variable";
        return r;
    }

    std::string systemPrompt =
        "You are a file analysis assistant. Analyze the file content and return a JSON response. "
        "Extract the file type accurately (e.g., C++ source, Python script, Markdown doc, JSON config, text file). "
        "Keep the summary under 200 Chinese characters. Return EXACTLY this JSON format:\n"
        "{\"summary\": \"...\", \"keywords\": \"...\", \"fileType\": \"...\", \"keySentences\": \"...\"}";

    std::string userContent =
        "File name: " + fileName + "\n"
        "File content (first 8000 chars):\n" + content.substr(0, 8000);

    APIBridge::AIResponse aiResp = api()->chat(systemPrompt, userContent);

    if (aiResp.success) {
        return parsePreviewResponse(aiResp.content);
    }

    // API failed — fallback
    r.success = true;
    r.summary = content.substr(0, std::min<size_t>(200, content.size()));
    r.keywords = fileName;
    r.fileType = "unknown";
    r.errorMsg = aiResp.errorMsg;
    return r;
}

void AIFilePreview::previewAsync(int64_t fileId, const std::string& content, const std::string& fileName, PreviewCallback cb) {
    if (!isAIEnabled()) {
        PreviewResult r;
        r.success = true;
        r.summary = content.substr(0, std::min<size_t>(200, content.size()));
        r.fileType = "unknown";
        r.keywords = fileName;
        r.errorMsg = "AI disabled: set SILICONFLOW_API_KEY, OPENAI_API_KEY, or OLLAMA_HOST environment variable";
        if (cb) cb(r);
        return;
    }

    std::string systemPrompt =
        "You are a file analysis assistant. Analyze the file content and return a JSON response. "
        "Return EXACTLY: {\"summary\":\"...\",\"keywords\":\"...\",\"fileType\":\"...\",\"keySentences\":\"...\"}";

    std::string userContent = "File: " + fileName + "\nContent (first 8000 chars):\n" + content.substr(0, 8000);

    api()->chatAsync(systemPrompt, userContent, [fileId, cb, this](APIBridge::AIResponse aiResp) {
        PreviewResult r;
        if (aiResp.success) {
            r = parsePreviewResponse(aiResp.content);
        } else {
            r.success = false;
            r.errorMsg = aiResp.errorMsg;
        }
        if (cb) cb(r);
    });
}

PreviewResult AIFilePreview::parsePreviewResponse(const std::string& jsonContent) {
    PreviewResult r;
    try {
        auto v = aijson::Value::parse(jsonContent);
        r.success = true;
        r.summary = v.getString("summary", "");
        r.keywords = v.getString("keywords", "");
        r.fileType = v.getString("fileType", "unknown");
        r.keySentences = v.getString("keySentences", "");
    } catch (...) {
        r.success = false;
        r.errorMsg = "Failed to parse AI response";
    }
    return r;
}
