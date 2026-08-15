#ifndef APIBRIDGE_H
#define APIBRIDGE_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>

/**
 * APIBridge — Multi-provider AI API wrapper using Qt Network
 *
 * Supported providers:
 *   - SiliconFlow (default): https://api.siliconflow.cn/v1   (OpenAI-compatible)
 *   - OpenAI:               https://api.openai.com/v1        (OpenAI-compatible)
 *   - Ollama (local):       http://localhost:11434           (native /api/chat + /api/embed protocol — NOT OpenAI-compatible)
 *   - Custom:               any OpenAI-compatible endpoint
 *
 * Provider selection (priority order):
 *   1. server.conf "ai" section  →  explicit override
 *   2. SILICONFLOW_API_KEY env   →  SiliconFlow provider
 *   3. OPENAI_API_KEY env        →  OpenAI provider (legacy)
 *   4. OLLAMA_HOST env           →  Ollama local provider (no API key needed)
 *   5. Neither                   →  AI disabled
 *
 * Features:
 *   - chat() — text completion (deepseek-ai/DeepSeek-V3 / gpt-4o-mini / qwen2.5:7b)
 *   - embedding() — vector generation (BAAI/bge-m3 / text-embedding-3-small / bge-m3)
 *   - vision() — image analysis (base64; not supported on Ollama provider)
 *   - All methods support sync (blocking event loop) and async (callback)
 *   - Ollama branch uses its own request/response formats (/api/chat, /api/embed,
 *     response read from "message.content" / "embeddings[0]"), no Authorization header.
 *   - Degradation: AI disabled silently when no provider configured
 *   - Timeout: 15s chat, 10s embedding
 */

// ============================================================================
// AiConfig — provider configuration (from server.conf or env vars)
// ============================================================================
struct AiConfig {
    std::string provider;         // "siliconflow" | "openai" | "ollama" | "custom"
    std::string apiKey;           // empty for ollama (local service, no auth)
    std::string baseUrl;
    std::string chatModel;
    std::string embeddingModel;

    AiConfig() : provider("siliconflow") {}

    bool isValid() const {
        // Ollama is a local service: no API key required, only a base URL.
        if (provider == "ollama") return !baseUrl.empty();
        return !apiKey.empty() && !baseUrl.empty();
    }

    // Pre-built configs
    static AiConfig siliconflow(const std::string& key) {
        AiConfig c;
        c.provider = "siliconflow";
        c.apiKey = key;
        c.baseUrl = "https://api.siliconflow.cn/v1";
        c.chatModel = "deepseek-ai/DeepSeek-V3";
        c.embeddingModel = "BAAI/bge-m3";
        return c;
    }

    static AiConfig openai(const std::string& key) {
        AiConfig c;
        c.provider = "openai";
        c.apiKey = key;
        c.baseUrl = "https://api.openai.com/v1";
        c.chatModel = "gpt-4o-mini";
        c.embeddingModel = "text-embedding-3-small";
        return c;
    }

    // Ollama — fully local, no API key. host may be "127.0.0.1:11434" or "http://host:port".
    static AiConfig ollama(const std::string& host = "") {
        AiConfig c;
        c.provider = "ollama";
        c.apiKey = "";                                  // local service — no auth
        std::string h = host;
        if (!h.empty() && h.find("http://") != 0 && h.find("https://") != 0) {
            h = "http://" + h;                          // accept bare "host:port"
        }
        c.baseUrl = h.empty() ? "http://localhost:11434" : h;
        c.chatModel = "qwen2.5:7b";
        c.embeddingModel = "bge-m3";
        return c;
    }
};

class APIBridge : public QObject {
    Q_OBJECT
public:
    struct AIResponse {
        bool        success = false;
        std::string content;       // text response
        std::string errorMsg;
        int         tokensUsed = 0;
        std::string model;
    };

    struct EmbeddingResult {
        bool        success = false;
        std::vector<float> embedding;
        std::string errorMsg;
    };

    struct VisionResult {
        bool        success = false;
        std::string description;
        std::string textContent;    // extracted text
        std::string errorMsg;
    };

    static APIBridge* instance();

    // ---- Lifecycle ----
    bool isEnabled() const { return m_enabled; }
    std::string providerName() const { return m_config.provider; }

    // Call after server.conf is loaded (tcpkernel::boolopen).
    // Overrides env-var detection with explicit config.
    void initFromConfig(const AiConfig& config);

    // === Chat ===
    // Synchronous (blocks event loop via QEventLoop::exec)
    AIResponse chat(const std::string& systemPrompt, const std::string& userContent);
    // Async (callback on completion)
    void chatAsync(const std::string& systemPrompt, const std::string& userContent,
                   std::function<void(AIResponse)> callback);

    // === Embedding ===
    EmbeddingResult embedding(const std::string& text);
    void embeddingAsync(const std::string& text,
                        std::function<void(EmbeddingResult)> callback);

    // === Vision ===
    VisionResult vision(const std::string& prompt, const std::string& imageBase64);
    void visionAsync(const std::string& prompt, const std::string& imageBase64,
                     std::function<void(VisionResult)> callback);

    // === Model info (reflects current config) ===
    std::string chatModel() const      { return m_config.chatModel; }
    std::string embeddingModel() const { return m_config.embeddingModel; }

private:
    explicit APIBridge(QObject* parent = nullptr);
    static APIBridge* s_instance;

    QNetworkAccessManager* m_manager;
    AiConfig m_config;
    bool m_enabled;

    // Auto-detect provider from environment variables (called in constructor)
    void detectFromEnv();

    QNetworkRequest buildRequest(const std::string& endpoint);
    QJsonObject chatBody(const std::string& systemPrompt, const std::string& userContent);
    QJsonObject embeddingBody(const std::string& text);
    AIResponse parseChatResponse(const QByteArray& data);
    EmbeddingResult parseEmbeddingResponse(const QByteArray& data);

    // ---- Ollama branch (native /api/chat + /api/embed protocol) ----
    bool isOllama() const;
    QJsonObject ollamaChatBody(const std::string& systemPrompt, const std::string& userContent);
    QJsonObject ollamaEmbeddingBody(const std::string& text);
    AIResponse parseOllamaChatResponse(const QByteArray& data);
    EmbeddingResult parseOllamaEmbeddingResponse(const QByteArray& data);
    AIResponse ollamaChat(const std::string& systemPrompt, const std::string& userContent);
    void ollamaChatAsync(const std::string& systemPrompt, const std::string& userContent,
                         std::function<void(AIResponse)> callback);
    EmbeddingResult ollamaEmbedding(const std::string& text);
    void ollamaEmbeddingAsync(const std::string& text,
                              std::function<void(EmbeddingResult)> callback);
};

#endif
