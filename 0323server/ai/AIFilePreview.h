#ifndef AIFILEPREVIEW_H
#define AIFILEPREVIEW_H

#include "AIServiceBase.h"
#include <string>
#include <functional>

struct PreviewResult {
    bool        success = false;
    std::string summary;       // 约 200 字的中文摘要
    std::string keywords;      // 逗号分隔的 5-8 个关键词
    std::string keySentences;  // 3 个关键句，换行分隔
    std::string fileType;      // 推断出的文件类型
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
