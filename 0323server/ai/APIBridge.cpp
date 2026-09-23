#include "APIBridge.h"
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <cstring>

APIBridge* APIBridge::s_instance = nullptr;

APIBridge* APIBridge::instance() {
    if (!s_instance) s_instance = new APIBridge();
    return s_instance;
}

APIBridge::APIBridge(QObject* parent) : QObject(parent), m_enabled(false), m_manager(nullptr) {
    detectFromEnv();
}

// ============================================================================
// detectFromEnv — 优先级: SILICONFLOW_API_KEY > OPENAI_API_KEY > OLLAMA_HOST > 禁用
// ============================================================================
void APIBridge::detectFromEnv() {
    // 1) 尝试 SiliconFlow
    const char* sfKey = std::getenv("SILICONFLOW_API_KEY");
    if (sfKey && strlen(sfKey) > 0) {
        m_config = AiConfig::siliconflow(sfKey);
        m_enabled = true;
        m_manager = new QNetworkAccessManager(this);
        printf("APIBridge: AI enabled — provider=siliconflow, chat=%s, embedding=%s\n",
               m_config.chatModel.c_str(), m_config.embeddingModel.c_str());
        return;
    }

    // 2) 回退: OpenAI（旧版兼容）
    const char* oaKey = std::getenv("OPENAI_API_KEY");
    if (oaKey && strlen(oaKey) > 0) {
        m_config = AiConfig::openai(oaKey);
        m_enabled = true;
        m_manager = new QNetworkAccessManager(this);
        printf("APIBridge: AI enabled — provider=openai, chat=%s, embedding=%s\n",
               m_config.chatModel.c_str(), m_config.embeddingModel.c_str());
        return;
    }

    // 3) Ollama（本地服务，无需 API Key）
    const char* ollamaHost = std::getenv("OLLAMA_HOST");
    if (ollamaHost && strlen(ollamaHost) > 0) {
        m_config = AiConfig::ollama(ollamaHost);
        m_enabled = true;
        m_manager = new QNetworkAccessManager(this);
        printf("APIBridge: AI enabled — provider=ollama (local), host=%s, chat=%s, embedding=%s\n",
               m_config.baseUrl.c_str(), m_config.chatModel.c_str(), m_config.embeddingModel.c_str());
        return;
    }

    // 4) 无 Key / 无 host → 禁用
    m_enabled = false;
    m_manager = nullptr;
    printf("APIBridge: AI disabled (set SILICONFLOW_API_KEY, OPENAI_API_KEY, or OLLAMA_HOST to enable)\n");
}

// ============================================================================
// initFromConfig — 用 server.conf 中的显式配置覆盖环境变量检测
// ============================================================================
void APIBridge::initFromConfig(const AiConfig& config) {
    if (!config.isValid()) {
        printf("APIBridge: initFromConfig called with invalid config, keeping env detection\n");
        return;
    }

    // 若重复初始化，先清理旧的 manager
    if (m_manager) {
        delete m_manager;
        m_manager = nullptr;
    }

    m_config = config;
    m_enabled = true;

    // 创建新的 manager（构造函数已设置 QObject 父对象，所有权归属 APIBridge）
    m_manager = new QNetworkAccessManager(this);

    printf("APIBridge: AI enabled from server.conf — provider=%s, base_url=%s, chat=%s, embedding=%s\n",
           m_config.provider.c_str(), m_config.baseUrl.c_str(),
           m_config.chatModel.c_str(), m_config.embeddingModel.c_str());
}

// ============================================================================
// HTTP 辅助函数
// ============================================================================
QNetworkRequest APIBridge::buildRequest(const std::string& endpoint) {
    QNetworkRequest req(QUrl(QString::fromStdString(m_config.baseUrl + endpoint)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // 未配置 Key 时跳过 Authorization 头（如本地 Ollama）
    if (!m_config.apiKey.empty()) {
        req.setRawHeader("Authorization", ("Bearer " + m_config.apiKey).c_str());
    }
    return req;
}

QJsonObject APIBridge::chatBody(const std::string& systemPrompt, const std::string& userContent) {
    QJsonObject body;
    body["model"] = QString::fromStdString(m_config.chatModel);
    body["max_tokens"] = 500;
    body["temperature"] = 0.3;

    QJsonArray messages;
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = QString::fromStdString(systemPrompt);
    messages.append(sysMsg);

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = QString::fromStdString(userContent);
    messages.append(userMsg);

    body["messages"] = messages;

    // 请求 JSON 格式输出
    QJsonObject responseFmt;
    responseFmt["type"] = "json_object";
    body["response_format"] = responseFmt;

    return body;
}

APIBridge::AIResponse APIBridge::parseChatResponse(const QByteArray& data) {
    AIResponse r;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        r.success = false;
        r.errorMsg = "Failed to parse JSON response";
        return r;
    }
    QJsonObject root = doc.object();
    if (root.contains("error")) {
        r.success = false;
        r.errorMsg = root["error"].toObject()["message"].toString().toStdString();
        return r;
    }

    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) {
        r.success = false;
        r.errorMsg = "No choices in response";
        return r;
    }

    r.success = true;
    r.content = choices[0].toObject()["message"].toObject()["content"].toString().toStdString();
    r.tokensUsed = root["usage"].toObject()["total_tokens"].toInt();
    r.model = root["model"].toString().toStdString();
    return r;
}

// ============================================================================
// chat — 同步
// ============================================================================
APIBridge::AIResponse APIBridge::chat(const std::string& systemPrompt, const std::string& userContent) {
    if (isOllama()) return ollamaChat(systemPrompt, userContent);   // Ollama 协议分支
    if (!m_enabled) { AIResponse r; r.success = false; r.errorMsg = "AI disabled: no API key configured"; return r; }

    QNetworkReply* reply = m_manager->post(
        buildRequest("/chat/completions"),
        QJsonDocument(chatBody(systemPrompt, userContent)).toJson());

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(15000);

    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        std::string err = (reply->error() == QNetworkReply::OperationCanceledError)
            ? "Chat request timed out (15s)" : reply->errorString().toStdString();
        delete reply;
        AIResponse r; r.success = false; r.errorMsg = err; return r;
    }

    AIResponse r = parseChatResponse(reply->readAll());
    delete reply;
    return r;
}

void APIBridge::chatAsync(const std::string& systemPrompt, const std::string& userContent,
                           std::function<void(AIResponse)> callback) {
    if (isOllama()) { ollamaChatAsync(systemPrompt, userContent, callback); return; }   // Ollama 分支
    if (!m_enabled) {
        if (callback) { AIResponse r; r.success = false; r.errorMsg = "AI disabled"; callback(r); }
        return;
    }

    QNetworkReply* reply = m_manager->post(
        buildRequest("/chat/completions"),
        QJsonDocument(chatBody(systemPrompt, userContent)).toJson());

    QTimer* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(15000);

    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, callback]() {
        timer->stop();
        AIResponse r;
        if (reply->error() != QNetworkReply::NoError) {
            r.success = false;
            r.errorMsg = (reply->error() == QNetworkReply::OperationCanceledError)
                ? "Chat request timed out (15s)" : reply->errorString().toStdString();
        } else {
            r = parseChatResponse(reply->readAll());
        }
        if (callback) callback(r);
        delete reply;
    });
}

// ============================================================================
// embedding — 同步
// ============================================================================
QJsonObject APIBridge::embeddingBody(const std::string& text) {
    QJsonObject body;
    body["model"] = QString::fromStdString(m_config.embeddingModel);
    body["input"] = QString::fromStdString(text);
    return body;
}

APIBridge::EmbeddingResult APIBridge::parseEmbeddingResponse(const QByteArray& data) {
    EmbeddingResult r;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        r.success = false; r.errorMsg = "Failed to parse JSON"; return r;
    }
    QJsonObject root = doc.object();
    if (root.contains("error")) {
        r.success = false;
        r.errorMsg = root["error"].toObject()["message"].toString().toStdString();
        return r;
    }
    QJsonArray dataArr = root["data"].toArray();
    if (dataArr.isEmpty()) { r.success = false; r.errorMsg = "No data array"; return r; }
    QJsonArray emb = dataArr[0].toObject()["embedding"].toArray();
    r.success = true;
    for (int i = 0; i < emb.size(); i++) {
        r.embedding.push_back((float)emb[i].toDouble());
    }
    return r;
}

APIBridge::EmbeddingResult APIBridge::embedding(const std::string& text) {
    if (isOllama()) return ollamaEmbedding(text);   // Ollama 协议分支
    if (!m_enabled) return {};
    QNetworkReply* reply = m_manager->post(
        buildRequest("/embeddings"),
        QJsonDocument(embeddingBody(text)).toJson());
    QEventLoop loop;
    QTimer timer; timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(10000);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        delete reply;
        return {};
    }
    EmbeddingResult r = parseEmbeddingResponse(reply->readAll());
    delete reply;
    return r;
}

void APIBridge::embeddingAsync(const std::string& text,
                                std::function<void(EmbeddingResult)> callback) {
    if (isOllama()) { ollamaEmbeddingAsync(text, callback); return; }   // Ollama 分支
    if (!m_enabled) { if (callback) callback({}); return; }
    QNetworkReply* reply = m_manager->post(
        buildRequest("/embeddings"),
        QJsonDocument(embeddingBody(text)).toJson());
    QTimer* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(10000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, callback]() {
        timer->stop();
        EmbeddingResult r;
        if (reply->error() != QNetworkReply::NoError) {
            r.success = false; r.errorMsg = reply->errorString().toStdString();
        } else {
            r = parseEmbeddingResponse(reply->readAll());
        }
        if (callback) callback(r);
        delete reply;
    });
}

// ============================================================================
// vision — 同步（使用 chat 端点携带图像内容）
// ============================================================================
APIBridge::VisionResult APIBridge::vision(const std::string& prompt, const std::string& imageBase64) {
    if (isOllama()) {
        // Ollama /api/chat 的图像消息格式不同，此分支不支持。
        VisionResult r; r.success = false; r.errorMsg = "Ollama vision not supported";
        return r;
    }
    if (!m_enabled) return {};
    QJsonObject body;
    body["model"] = QString::fromStdString(m_config.chatModel);  // vision 同样使用 chat 模型
    body["max_tokens"] = 500;

    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = "user";

    QJsonArray content;
    QJsonObject textPart;
    textPart["type"] = "text";
    textPart["text"] = QString::fromStdString(prompt);
    content.append(textPart);

    QJsonObject imgPart;
    imgPart["type"] = "image_url";
    QJsonObject imgUrl;
    imgUrl["url"] = QString::fromStdString("data:image/png;base64," + imageBase64);
    imgPart["image_url"] = imgUrl;
    content.append(imgPart);

    userMsg["content"] = content;
    messages.append(userMsg);
    body["messages"] = messages;

    QNetworkReply* reply = m_manager->post(
        buildRequest("/chat/completions"), QJsonDocument(body).toJson());
    QEventLoop loop;
    QTimer timer; timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(20000);
    loop.exec();

    VisionResult r;
    if (reply->error() != QNetworkReply::NoError) {
        r.success = false;
        r.errorMsg = (reply->error() == QNetworkReply::OperationCanceledError)
            ? "Vision request timed out (20s)" : reply->errorString().toStdString();
    } else {
        AIResponse chatR = parseChatResponse(reply->readAll());
        r.success = chatR.success;
        r.description = chatR.content;
        r.textContent = chatR.content;
    }
    delete reply;
    return r;
}

void APIBridge::visionAsync(const std::string& prompt, const std::string& imageBase64,
                              std::function<void(VisionResult)> callback) {
    if (!m_enabled) { if (callback) callback({}); return; }
    VisionResult r = vision(prompt, imageBase64);
    if (callback) callback(r);
}

// ============================================================================
// Ollama 协议分支 — 本地服务，使用原生 /api/chat + /api/embed。
// 请求/响应格式与 OpenAI 不同:
//   chat:      POST {baseUrl}/api/chat  body {"model", "messages", "stream": false}
//              响应内容位于 data["message"]["content"]
//   embedding: POST {baseUrl}/api/embed body {"model", "input": text}
//              响应向量位于 data["embeddings"][0]  （嵌套数组）
//   auth:      无（apiKey 为空时跳过 Authorization 头）
// ============================================================================
bool APIBridge::isOllama() const {
    return m_config.provider == "ollama";
}

QJsonObject APIBridge::ollamaChatBody(const std::string& systemPrompt, const std::string& userContent) {
    QJsonObject body;
    body["model"] = QString::fromStdString(m_config.chatModel);

    QJsonArray messages;
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = QString::fromStdString(systemPrompt);
    messages.append(sysMsg);

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = QString::fromStdString(userContent);
    messages.append(userMsg);

    body["messages"] = messages;
    body["stream"] = false;   // 非流式 JSON 响应
    return body;
}

APIBridge::AIResponse APIBridge::parseOllamaChatResponse(const QByteArray& data) {
    AIResponse r;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        r.success = false;
        r.errorMsg = "Failed to parse JSON response";
        return r;
    }
    QJsonObject root = doc.object();
    if (root.contains("error")) {
        r.success = false;
        r.errorMsg = root["error"].toObject()["message"].toString().toStdString();
        return r;
    }

    // Ollama: 内容位于 "message"."content"（没有 "choices" 数组）
    QJsonObject msg = root["message"].toObject();
    if (msg.isEmpty()) {
        r.success = false;
        r.errorMsg = "No message in response";
        return r;
    }

    r.success = true;
    r.content = msg["content"].toString().toStdString();
    r.model = root["model"].toString().toStdString();
    r.tokensUsed = root["eval_count"].toInt();   // 输出 tokens 数（Ollama 命名）
    return r;
}

APIBridge::AIResponse APIBridge::ollamaChat(const std::string& systemPrompt, const std::string& userContent) {
    AIResponse r;
    if (!m_enabled) { r.success = false; r.errorMsg = "AI disabled: no API key configured"; return r; }

    QNetworkReply* reply = m_manager->post(
        buildRequest("/api/chat"),
        QJsonDocument(ollamaChatBody(systemPrompt, userContent)).toJson());

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(15000);

    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        std::string err = (reply->error() == QNetworkReply::OperationCanceledError)
            ? "Chat request timed out (15s)" : reply->errorString().toStdString();
        delete reply;
        r.success = false; r.errorMsg = err; return r;
    }

    r = parseOllamaChatResponse(reply->readAll());
    delete reply;
    return r;
}

void APIBridge::ollamaChatAsync(const std::string& systemPrompt, const std::string& userContent,
                                std::function<void(AIResponse)> callback) {
    if (!m_enabled) {
        if (callback) { AIResponse r; r.success = false; r.errorMsg = "AI disabled"; callback(r); }
        return;
    }

    QNetworkReply* reply = m_manager->post(
        buildRequest("/api/chat"),
        QJsonDocument(ollamaChatBody(systemPrompt, userContent)).toJson());

    QTimer* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(15000);

    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, callback]() {
        timer->stop();
        AIResponse r;
        if (reply->error() != QNetworkReply::NoError) {
            r.success = false;
            r.errorMsg = (reply->error() == QNetworkReply::OperationCanceledError)
                ? "Chat request timed out (15s)" : reply->errorString().toStdString();
        } else {
            r = parseOllamaChatResponse(reply->readAll());
        }
        if (callback) callback(r);
        delete reply;
    });
}

QJsonObject APIBridge::ollamaEmbeddingBody(const std::string& text) {
    QJsonObject body;
    body["model"] = QString::fromStdString(m_config.embeddingModel);
    body["input"] = QString::fromStdString(text);
    return body;
}

APIBridge::EmbeddingResult APIBridge::parseOllamaEmbeddingResponse(const QByteArray& data) {
    EmbeddingResult r;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        r.success = false; r.errorMsg = "Failed to parse JSON"; return r;
    }
    QJsonObject root = doc.object();
    if (root.contains("error")) {
        r.success = false;
        r.errorMsg = root["error"].toObject()["message"].toString().toStdString();
        return r;
    }

    // Ollama /api/embed: "embeddings": [[...]] — 每个输入一个向量（嵌套数组）
    QJsonArray embeddings = root["embeddings"].toArray();
    if (embeddings.isEmpty()) {
        r.success = false; r.errorMsg = "No embeddings array"; return r;
    }
    QJsonArray emb = embeddings[0].toArray();
    if (emb.isEmpty()) {
        r.success = false; r.errorMsg = "Empty embedding vector"; return r;
    }

    r.success = true;
    for (int i = 0; i < emb.size(); i++) {
        r.embedding.push_back((float)emb[i].toDouble());
    }
    return r;
}

APIBridge::EmbeddingResult APIBridge::ollamaEmbedding(const std::string& text) {
    EmbeddingResult r;
    if (!m_enabled) { r.success = false; r.errorMsg = "AI disabled"; return r; }

    QNetworkReply* reply = m_manager->post(
        buildRequest("/api/embed"),
        QJsonDocument(ollamaEmbeddingBody(text)).toJson());

    QEventLoop loop;
    QTimer timer; timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(10000);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        std::string err = (reply->error() == QNetworkReply::OperationCanceledError)
            ? "Embedding request timed out (10s)" : reply->errorString().toStdString();
        delete reply;
        r.success = false; r.errorMsg = err; return r;
    }

    r = parseOllamaEmbeddingResponse(reply->readAll());
    delete reply;
    return r;
}

void APIBridge::ollamaEmbeddingAsync(const std::string& text,
                                     std::function<void(EmbeddingResult)> callback) {
    if (!m_enabled) { if (callback) callback({}); return; }

    QNetworkReply* reply = m_manager->post(
        buildRequest("/api/embed"),
        QJsonDocument(ollamaEmbeddingBody(text)).toJson());

    QTimer* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(10000);

    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, callback]() {
        timer->stop();
        EmbeddingResult r;
        if (reply->error() != QNetworkReply::NoError) {
            r.success = false;
            r.errorMsg = (reply->error() == QNetworkReply::OperationCanceledError)
                ? "Embedding request timed out (10s)" : reply->errorString().toStdString();
        } else {
            r = parseOllamaEmbeddingResponse(reply->readAll());
        }
        if (callback) callback(r);
        delete reply;
    });
}
