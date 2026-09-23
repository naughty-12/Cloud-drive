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
 * APIBridge — 基于 Qt Network 的多 Provider AI API 封装
 *
 * 支持的 Provider:
 *   - SiliconFlow (默认): https://api.siliconflow.cn/v1   (OpenAI 兼容)
 *   - OpenAI:               https://api.openai.com/v1        (OpenAI 兼容)
 *   - Ollama (本地):       http://localhost:11434           (原生 /api/chat + /api/embed 协议 — 非 OpenAI 兼容)
 *   - 自定义:              任意 OpenAI 兼容端点
 *
 * Provider 选择（优先级从高到低）:
 *   1. server.conf 的 "ai" 段  →  显式覆盖
 *   2. SILICONFLOW_API_KEY 环境变量  →  SiliconFlow Provider
 *   3. OPENAI_API_KEY 环境变量       →  OpenAI Provider（旧版兼容）
 *   4. OLLAMA_HOST 环境变量          →  Ollama 本地 Provider（无需 API Key）
 *   5. 以上皆无                    →  AI 禁用
 *
 * 功能:
 *   - chat() — 文本补全 (deepseek-ai/DeepSeek-V3 / gpt-4o-mini / qwen2.5:7b)
 *   - embedding() — 向量生成 (BAAI/bge-m3 / text-embedding-3-small / bge-m3)
 *   - vision() — 图像分析 (base64; Ollama Provider 不支持)
 *   - 所有方法均支持同步（阻塞事件循环）与异步（回调）两种模式
 *   - Ollama 分支使用自身的请求/响应格式 (/api/chat, /api/embed,
 *     响应内容从 "message.content" / "embeddings[0]" 读取)，不发送 Authorization 头
 *   - 降级策略: 未配置任何 Provider 时静默禁用 AI
 *   - 超时: chat 15 秒, embedding 10 秒
 */

// ============================================================================
// AiConfig — Provider 配置（来自 server.conf 或环境变量）
// ============================================================================
struct AiConfig {
    std::string provider;         // "siliconflow" | "openai" | "ollama" | "custom"
    std::string apiKey;           // ollama 为空（本地服务，无需鉴权）
    std::string baseUrl;
    std::string chatModel;
    std::string embeddingModel;

    AiConfig() : provider("siliconflow") {}

    bool isValid() const {
        // Ollama 是本地服务：无需 API Key，只需 base URL。
        if (provider == "ollama") return !baseUrl.empty();
        return !apiKey.empty() && !baseUrl.empty();
    }

    // 预置配置
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

    // Ollama — 完全本地化，无需 API Key。host 可以是 "127.0.0.1:11434" 或 "http://host:port"。
    static AiConfig ollama(const std::string& host = "") {
        AiConfig c;
        c.provider = "ollama";
        c.apiKey = "";                                  // 本地服务 — 无需鉴权
        std::string h = host;
        if (!h.empty() && h.find("http://") != 0 && h.find("https://") != 0) {
            h = "http://" + h;                          // 接受直接写 "host:port" 的形式
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
        std::string content;       // 文本响应
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
        std::string textContent;    // 提取出的文本
        std::string errorMsg;
    };

    static APIBridge* instance();

    // ---- 生命周期 ----
    bool isEnabled() const { return m_enabled; }
    std::string providerName() const { return m_config.provider; }

    // 在 server.conf 加载完成后调用（tcpkernel::boolopen）。
    // 用显式配置覆盖环境变量自动检测。
    void initFromConfig(const AiConfig& config);

    // === Chat（对话） ===
    // 同步（通过 QEventLoop::exec 阻塞事件循环）
    AIResponse chat(const std::string& systemPrompt, const std::string& userContent);
    // 异步（完成时回调）
    void chatAsync(const std::string& systemPrompt, const std::string& userContent,
                   std::function<void(AIResponse)> callback);

    // === Embedding（向量嵌入） ===
    EmbeddingResult embedding(const std::string& text);
    void embeddingAsync(const std::string& text,
                        std::function<void(EmbeddingResult)> callback);

    // === Vision（图像理解） ===
    VisionResult vision(const std::string& prompt, const std::string& imageBase64);
    void visionAsync(const std::string& prompt, const std::string& imageBase64,
                     std::function<void(VisionResult)> callback);

    // === 模型信息（反映当前配置） ===
    std::string chatModel() const      { return m_config.chatModel; }
    std::string embeddingModel() const { return m_config.embeddingModel; }

private:
    explicit APIBridge(QObject* parent = nullptr);
    static APIBridge* s_instance;

    QNetworkAccessManager* m_manager;
    AiConfig m_config;
    bool m_enabled;

    // 从环境变量自动检测 Provider（在构造函数中调用）
    void detectFromEnv();

    QNetworkRequest buildRequest(const std::string& endpoint);
    QJsonObject chatBody(const std::string& systemPrompt, const std::string& userContent);
    QJsonObject embeddingBody(const std::string& text);
    AIResponse parseChatResponse(const QByteArray& data);
    EmbeddingResult parseEmbeddingResponse(const QByteArray& data);

    // ---- Ollama 分支（原生 /api/chat + /api/embed 协议） ----
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
