#ifndef AITAGSERVICE_H
#define AITAGSERVICE_H

#include "AIServiceBase.h"
#include <string>
#include <vector>
#include <functional>

struct TagResult {
    bool        success = false;
    std::vector<std::string> tags;
    std::vector<std::string> newTagSuggestions;
    std::string errorMsg;
};

class AITagService : public AIServiceBase {
public:
    using TagCallback = std::function<void(const TagResult&)>;

    TagResult tagFile(int64_t fileId, const std::string& content, const std::string& fileName);
    void tagFileAsync(int64_t fileId, const std::string& content, const std::string& fileName, TagCallback cb);

private:
    TagResult parseTagResponse(const std::string& jsonContent);
};
#endif
