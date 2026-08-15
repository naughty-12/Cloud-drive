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
// detectFromEnv — priority: SILICONFLOW_API_KEY > OPENAI_API_KEY > OLLAMA_HOST > disabled
// ============================================================================
void APIBridge::detectFromEnv() {
    // 1) Try SiliconFlow
    const char* sfKey = std::getenv("SILICONFLOW_API_KEY");
    if (sfKey && strlen(sfKey) > 0) {
        m_config = AiConfig::siliconflow(sfKey);
        m_enabled = true;
        m_manager = new QNetworkAccessManager(this);
        printf("APIBridge: AI enabled — provider=siliconflow, chat=%s, embedding=%s\n",
               m_config.chatModel.c_str(), m_config.embeddingModel.c_str());
        return;
    }

    // 2) Fallback: OpenAI (legacy)
    const char* oaKey = std::getenv("OPENAI_API_KEY");
    if (oaKey && strlen(oaKey) > 0) {
        m_config = AiConfig::openai(oaKey);
        m_enabled = true;
        m_manager = new QNetworkAccessManager(this);
        printf("APIBridge: AI enabled — provider=openai, chat=%s, embedding=%s\n",
               m_config.chatModel.c_str(), m_config.embeddingModel.c_str());
        return;
    }

    // 3) Ollama (local service, no API key needed)
    const char* ollamaHost = std::getenv("OLLAMA_HOST");
    if (ollamaHost && strlen(ollamaHost) > 0) {
        m_config = AiConfig::ollama(ollamaHost);
        m_enabled = true;
        m_manager = new QNetworkAccessManager(this);
        printf("APIBridge: AI enabled — provider=ollama (local), host=%s, chat=%s, embedding=%s\n",
               m_config.baseUrl.c_str(), m_config.chatModel.c_str(), m_config.embeddingModel.c_str());
        return;
    }

    // 4) No key / no host → disabled
    m_enabled = false;
    m_manager = nullptr;
    printf("APIBridge: AI disabled (set SILICONFLOW_API_KEY, OPENAI_API_KEY, or OLLAMA_HOST to enable)\n");
}

// ============================================================================
// initFromConfig — override env detection with explicit config from server.conf
// ============================================================================
void APIBridge::initFromConfig(const AiConfig& config) {
    if (!config.isValid()) {
        printf("APIBridge: initFromConfig called with invalid config, keeping env detection\n");
        return;
    }

    // Clean up old manager if re-initializing
    if (m_manager) {
        delete m_manager;
        m_manager = nullptr;
    }

    m_config = config;
    m_enabled = true;

    // Create new manager (ownership stays with APIBridge as QObject parent is set in constructor)
    m_manager = new QNetworkAccessManager(this);

    printf("APIBridge: AI enabled from server.conf — provider=%s, base_url=%s, chat=%s, embedding=%s\n",
           m_config.provider.c_str(), m_config.baseUrl.c_str(),
           m_config.chatModel.c_str(), m_config.embeddingModel.c_str());
}

// ============================================================================
// HTTP helpers
// ============================================================================
QNetworkRequest APIBridge::buildRequest(const std::string& endpoint) {
    QNetworkRequest req(QUrl(QString::fromStdString(m_config.baseUrl + endpoint)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // Skip Authorization header when no key is configured (e.g. local Ollama)
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

    // Request JSON format output
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
// chat — synchronous
// ============================================================================
APIBridge::AIResponse APIBridge::chat(const std::string& systemPrompt, const std::string& userContent) {
    if (isOllama()) return ollamaChat(systemPrompt, userContent);   // Ollama protocol branch
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
    if (isOllama()) { ollamaChatAsync(systemPrompt, userContent, callback); return; }   // Ollama branch
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
// embedding — synchronous
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
    if (isOllama()) return ollamaEmbedding(text);   // Ollama protocol branch
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
    if (isOllama()) { ollamaEmbeddingAsync(text, callback); return; }   // Ollama branch
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
// vision — synchronous (uses chat endpoint with image content)
// ============================================================================
APIBridge::VisionResult APIBridge::vision(const std::string& prompt, const std::string& imageBase64) {
    if (isOllama()) {
        // Ollama /api/chat has a different image message format; not supported in this branch.
        VisionResult r; r.success = false; r.errorMsg = "Ollama vision not supported";
        return r;
    }
    if (!m_enabled) return {};
    QJsonObject body;
    body["model"] = QString::fromStdString(m_config.chatModel);  // use chat model for vision too
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
// Ollama protocol branch — local service, native /api/chat + /api/embed.
// Request/response formats differ from OpenAI:
//   chat:      POST {baseUrl}/api/chat  body {"model", "messages", "stream": false}
//              response content at data["message"]["content"]
//   embedding: POST {baseUrl}/api/embed body {"model", "input": text}
//              response vector at data["embeddings"][0]  (nested array)
//   auth:      none (Authorization header skipped when apiKey is empty)
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
    body["stream"] = false;   // non-streaming JSON response
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

    // Ollama: content lives under "message"."content" (no "choices" array)
    QJsonObject msg = root["message"].toObject();
    if (msg.isEmpty()) {
        r.success = false;
        r.errorMsg = "No message in response";
        return r;
    }

    r.success = true;
    r.content = msg["content"].toString().toStdString();
    r.model = root["model"].toString().toStdString();
    r.tokensUsed = root["eval_count"].toInt();   // output tokens (Ollama naming)
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

    // Ollama /api/embed: "embeddings": [[...]] — one vector per input (nested array)
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
