#ifndef AIFILEPREVIEW_H
#define AIFILEPREVIEW_H

#include "AIServiceBase.h"
#include <string>
#include <functional>

struct PreviewResult {
    bool        success = false;
    std::string summary;       // ~200 chars Chinese summary
    std::string keywords;      // comma-separated, 5-8 keywords
    std::string keySentences;  // 3 key sentences, newline-separated
    std::string fileType;      // inferred file type
    std::string errorMsg;
};

class AIFilePreview : public AIServiceBase {
public:
    using PreviewCallback = std::function<void(const PreviewResult&)>;

    PreviewResult preview(int64_t fileId, const std::string& content, const std::string& fileName);
    void previewAsync(int64_t fileId, const std::string& content, const std::string& fileName, PreviewCallback cb);

private:
    std::string extractText(const std::string& filePath);
    PreviewResult parsePreviewResponse(const std::string& jsonContent);
};
#endif
