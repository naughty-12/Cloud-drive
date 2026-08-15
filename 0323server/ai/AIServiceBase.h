#ifndef AISERVICEBASE_H
#define AISERVICEBASE_H

#include "APIBridge.h"
#include <string>

/**
 * AIServiceBase — Base class for all AI service modules
 * Provides shared access to APIBridge singleton and enable-check helpers.
 */
class AIServiceBase {
protected:
    APIBridge* api() const { return APIBridge::instance(); }
    bool isAIEnabled() const { return api()->isEnabled(); }
};

#endif
