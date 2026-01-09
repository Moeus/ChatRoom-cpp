#include <drogon/HttpClient.h>
#include <drogon/WebSocketController.h>
#include <drogon/drogon.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif
using namespace drogon;
using json = nlohmann::json;

/* ================= Constants & Utils ================= */
const int MAX_HISTORY = 10000;

// 获取当前时间 (毫秒)
// 处理过程：获取系统当前时间，转换为毫秒数
// 返回值：long long - 自 Epoch 以来的毫秒数
long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// 格式化聊天日志
// 处理过程：将昵称、ID 和内容拼接成特定格式的字符串
// 返回值：string - 格式化后的日志字符串
inline std::string formatChatLog(const std::string& nickname,
                                 const std::string& userId,
                                 const std::string& content) {
    return nickname + "@" + userId + ": " + content;
}

/* ================= UserRepository ================= */
struct User {
    std::string id;
    std::string nickname;
    std::string avatar;
};

// 用户仓库类
// 负责管理在线用户的存储、添加、移除和查询
class UserRepository {
private:
    std::unordered_map<std::string, User> users_;  // 存储在线用户的哈希表
    std::mutex mutex_;                             // 互斥锁，保护用户数据

public:
    // 添加用户
    // 处理过程：加锁，检查用户是否存在，不存在则添加
    // 返回值：pair<bool, string> - {成功与否, 错误信息}
    std::pair<bool, std::string> addUser(const std::string& id,
                                         const std::string& nickname,
                                         const std::string& avatar) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (users_.find(id) != users_.end()) {
            return {false, "用户已在线"};
        }
        users_[id] = User{id, nickname, avatar};
        return {true, ""};
    }

    // 移除用户
    // 处理过程：加锁，从 Map 中移除指定用户 ID
    // 返回值：无
    void removeUser(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        users_.erase(id);
    }

    // 检查用户是否存在
    // 处理过程：加读锁，查询 Map
    // 返回值：bool - 存在返回 true
    bool userExists(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return users_.find(id) != users_.end();
    }
};

/* ================= MessageRepository ================= */
// 消息仓库类
// 负责聊天消息的存储和历史记录查询
class MessageRepository {
private:
    std::deque<json> messages_;  // 消息队列，用于存储最近的消息
    std::mutex mutex_;           // 互斥锁，保护消息队列

public:
    MessageRepository() {
        initPath();
        load();
    }

    ~MessageRepository() { save(); }

    // 保存消息
    // 处理过程：补充时间戳、类型、ID，存入内存队列，并维持最大历史记录数量
    // 返回值：std::optional<json> - 成功返回处理后的完整消息 JSON，失败返回
    // nullopt
    std::optional<json> saveMessage(json& msg, const std::string& defaultType) {
        try {
            if (!msg.contains("time"))
                msg["time"] = nowMs();
            if (!msg.contains("type"))
                msg["type"] = defaultType;
            // 生成简单 ID
            if (!msg.contains("id")) {
                msg["id"] =
                    "msg_" + std::to_string(msg["time"].get<long long>());
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                messages_.push_back(msg);
                if (messages_.size() > MAX_HISTORY) {
                    messages_.pop_front();
                }
            }
            return msg;
        } catch (const std::exception& e) {
            LOG_ERROR << "保存消息失败: " << e.what();
            return std::nullopt;
        }
    }

    // 获取历史消息
    // 处理过程：加锁，将队列中的消息转换为 JSON 数组
    // 返回值：json - 消息数组
    json getHistory() {
        std::lock_guard<std::mutex> lock(mutex_);
        json arr = json::array();
        for (const auto& msg : messages_) {
            arr.push_back(msg);
        }
        return arr;
    }

private:
    std::string dbPath_;

    void initPath() {
        std::filesystem::path exeDir;
#ifdef _WIN32
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        exeDir = std::filesystem::path(path).parent_path();
#else
        // Linux/Unix fallback (not strictly required by user but good practice)
        exeDir = std::filesystem::current_path();
#endif
        dbPath_ = (exeDir / "message.json").string();
    }

    void load() {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!std::filesystem::exists(dbPath_)) {
            LOG_INFO << "未发现历史消息文件: " << dbPath_;
            return;
        }

        try {
            std::ifstream f(dbPath_);
            if (f.is_open()) {
                json j;
                f >> j;
                if (j.is_array()) {
                    messages_.clear();
                    for (auto& m : j) {
                        messages_.push_back(m);
                    }
                    LOG_INFO << "成功加载 " << messages_.size()
                             << " 条历史消息 from " << dbPath_;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "加载历史消息失败: " << e.what();
        }
    }

    void save() {
        // limit lock scope? Destructor is usually exclusive, but just in case.
        // Actually for destructor, if other threads are accessing, we have
        // bigger problems. But let's lock to be safe if this is called manually
        // (though it's private).
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            std::ofstream f(dbPath_);
            if (f.is_open()) {
                json arr = json::array();
                for (const auto& m : messages_) {
                    arr.push_back(m);
                }
                f << arr.dump(4);  // pretty print
                LOG_INFO << "聊天记录已保存至: " << dbPath_;
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "保存历史消息失败: " << e.what();
        }
    }
};

auto globalUserRepo = std::make_shared<UserRepository>();
auto globalMessageRepo = std::make_shared<MessageRepository>();

/* ================= Broadcaster ================= */
// 广播器类
// 负责管理所有 WebSocket 连接，并支持广播消息
class Broadcaster {
private:
    std::mutex mutex_;  // 互斥锁，保护连接集合
    std::unordered_set<WebSocketConnectionPtr>
        conns_;  // 存储所有活跃的 WebSocket 连接

public:
    // 添加连接
    // 处理过程：加锁，将连接加入集合
    // 返回值：无
    void addConnection(const WebSocketConnectionPtr& conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        conns_.insert(conn);
    }

    // 移除连接
    // 处理过程：加锁，从集合中移除连接
    // 返回值：无
    void removeConnection(const WebSocketConnectionPtr& conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        conns_.erase(conn);
    }

    // 广播消息
    // 处理过程：加锁，遍历所有连接，如果是连接状态则发送消息
    // 返回值：无
    void broadcast(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& c : conns_) {
            if (c->connected()) {
                c->send(msg);
            }
        }
    }
};

Broadcaster globalBroadcaster;

/* ================= ChatBot ================= */
// 聊天机器人类
// 负责处理聊天消息，调用外部 API 生成回复
class ChatBot {
private:
    // 获取 API Key
    // 处理过程：返回硬编码的 API Key
    // 返回值：string - API Key
    static std::string getApiKey() {
        return "sk-e8d96cc5d63a4c9eb8acdcac9396b701";
    }

public:
    // 广播回复
    // 处理过程：构造机器人的消息 JSON 对象，保存并广播
    // 返回值：无
    static void broadcastBotMessage(const std::string& content) {
        json j;
        j["type"] = "msg";
        j["userId"] = "bot_001";
        j["nickname"] = "ChatBot";
        j["avatar"] = "🤖";
        j["content"] = content;

        if (auto m = globalMessageRepo->saveMessage(j, "msg")) {
            globalBroadcaster.broadcast(m->dump());
        }
    }

    // 处理消息
    // 处理过程：构造请求发送给 OpenAI 兼容接口，接收响应并广播
    // 返回值：无
    static void process(const std::string& query, const json& historyCtx) {
        auto client =
            HttpClient::newHttpClient("https://dashscope.aliyuncs.com");

        std::string systemPrompt =
            "你是一个多人聊天室中乐于助人的聊天机器人。"
            "用户的消息格式为: [昵称] (ID: <id>): <消息内容>。"
            "当你回复时，只发送你的消息内容，不要包含你自己的昵称/ID前缀。"
            "你要乐于助人、友好且简洁。"
            "回复用户消息时，只针对最后一条用户消息进行回复。"
            "请记住在消息内容的第一行使用 @昵称 "
            "来称呼用户，然后在第二行开始编写消息内容。";

        json messages = json::array();
        messages.push_back({{"role", "system"}, {"content", systemPrompt}});

        if (historyCtx.is_array()) {
            for (const auto& item : historyCtx) {
                if (!item.contains("userId") || !item.contains("nickname") ||
                    !item.contains("content"))
                    continue;
                std::string uid = item["userId"];
                std::string nick = item["nickname"];
                std::string content = item["content"];

                std::string attributedContent =
                    "[" + nick + "] (ID: " + uid + "): " + content;
                messages.push_back(
                    {{"role", "user"}, {"content", attributedContent}});
            }
        }

        // 构造请求
        json reqBody;
        reqBody["model"] = "qwen-flash";
        reqBody["messages"] = messages;

        auto req = HttpRequest::newHttpRequest();
        req->setMethod(drogon::Post);
        req->setPath("/compatible-mode/v1/chat/completions");
        req->setContentTypeCode(CT_APPLICATION_JSON);
        req->addHeader("Authorization", "Bearer " + getApiKey());
        req->setBody(reqBody.dump());

        client->sendRequest(req, [](ReqResult result,
                                    const HttpResponsePtr& response) {
            if (result == ReqResult::Ok && response->statusCode() == 200) {
                try {
                    auto body = json::parse(response->getBody());
                    if (body.contains("choices") && !body["choices"].empty()) {
                        std::string content =
                            body["choices"][0]["message"]["content"];
                        ChatBot::broadcastBotMessage(content);
                    }
                } catch (...) {
                    LOG_ERROR << "解析 OpenAI 响应失败";
                }
            } else {
                LOG_ERROR << "OpenAI API 错误: "
                          << (response ? response->getBody() : "超时/网络错误");
            }
        });
    }
};

/* ================= WebSocket Controller ================= */
// WebSocket 用户上下文
struct UserContext {
    std::string userId;
    std::string nickname;
    std::string avatar;
    bool loggedIn = false;
};
// WebSocket 控制器类
// 负责处理 WebSocket 连接建立、消息接收和断开连接事件
class ChatWebSocket : public WebSocketController<ChatWebSocket> {
private:
    // 处理登出逻辑
    // 处理过程：从全局用户仓库移除用户，更新上下文状态，记录日志，并广播登出/断开连接消息
    // 返回值：无
    void processLogout(const std::shared_ptr<UserContext>& ctx) {
        if (!ctx->loggedIn)
            return;

        globalUserRepo->removeUser(ctx->userId);
        ctx->loggedIn = false;

        LOG_INFO << formatChatLog(ctx->nickname, ctx->userId, "下线");

        json j{{"type", "logout"},
               {"userId", ctx->userId},
               {"nickname", ctx->nickname},
               {"avatar", ctx->avatar},
               {"content", "连接已关闭"}};

        if (auto m = globalMessageRepo->saveMessage(j, "logout")) {
            globalBroadcaster.broadcast(m->dump());
        }
    }

public:
    // 处理新消息
    // 处理过程：解析消息类型(login, msg,
    // logout)，执行相应业务逻辑（登录验证、消息存储广播、聊天机器人触发等）
    // 返回值：无
    void handleNewMessage(const WebSocketConnectionPtr& ws,
                          std::string&& message,
                          const WebSocketMessageType& type) override {
        if (type != WebSocketMessageType::Text)
            return;

        if (message == "ping") {
            ws->send("pong");
            return;
        }

        try {
            auto j = json::parse(message);
            auto ctx = ws->getContext<UserContext>();
            std::string t = j.value("type", "");

            // ---- 登录 ----
            if (t == "login") {
                std::string uid = j.value("userId", "");
                if (uid.empty())
                    return;

                std::string nick = j.value("nickname", "未知用户");
                std::string avt = j.value("avatar", "");

                auto [success, errMsg] =
                    globalUserRepo->addUser(uid, nick, avt);
                if (!success) {
                    j["state"]=false;
                    ws->send(j.dump());
                    return;
                }

                ctx->userId = uid;
                ctx->nickname = nick;
                ctx->avatar = avt;
                ctx->loggedIn = true;

                LOG_INFO << "登录成功: " << ctx->nickname << "@" << ctx->userId;
                LOG_INFO << formatChatLog(ctx->nickname, ctx->userId, "上线");

                auto hist = globalMessageRepo->getHistory();
                if (!hist.empty()) {
                    ws->send(
                        json{{"type", "history"}, {"content", hist}}.dump());
                }

                j["state"] = true;

                if (auto m = globalMessageRepo->saveMessage(j, "login")) {
                    globalBroadcaster.broadcast(m->dump());
                    ChatBot::broadcastBotMessage(
                        "@" + ctx->nickname +
                        " 你好! 欢迎来到聊天室! 我是聊天室的 AI 助手 "
                        "ChatBot。使用 @ChatBot 召唤我，就能与我进行对话交流");
                }
            }
            // ---- 消息 ----
            else if (t == "msg") {
                if (!ctx->loggedIn)
                    return;

                j["userId"] = ctx->userId;
                j["nickname"] = ctx->nickname;
                j["avatar"] = ctx->avatar;

                if (auto m = globalMessageRepo->saveMessage(j, "msg")) {
                    LOG_INFO << formatChatLog(ctx->nickname, ctx->userId,
                                              j.value("content", ""));
                    globalBroadcaster.broadcast(m->dump());

                    // ChatBot Trigger
                    std::string content = j.value("content", "");
                    if (content.find("@ChatBot") != std::string::npos) {
                        LOG_INFO << "触发聊天机器人";
                        json history = globalMessageRepo->getHistory();
                        ChatBot::process(content, history);
                    }
                }
            }
            // ---- 登出 ----
            else if (t == "logout") {
                if (!ctx->loggedIn)
                    return;
                processLogout(ctx);
                ws->forceClose();
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "消息处理失败: " << e.what();
        }
    }

    // 处理新连接
    // 处理过程：初始化用户上下文，将连接加入订阅管理器
    // 返回值：无
    void handleNewConnection(const HttpRequestPtr&,
                             const WebSocketConnectionPtr& ws) override {
        ws->setContext(std::make_shared<UserContext>());
        globalBroadcaster.addConnection(ws);
        LOG_INFO << "WebSocket 已连接";
    }

    // 处理连接断开
    // 处理过程：从订阅管理器移除连接，如果用户已登录则执行登出处理
    // 返回值：无
    void handleConnectionClosed(const WebSocketConnectionPtr& ws) override {
        globalBroadcaster.removeConnection(ws);
        auto ctx = ws->getContext<UserContext>();
        if (ctx && ctx->loggedIn) {
            LOG_INFO << "WebSocket 断开连接: " << ctx->nickname << "@"
                     << ctx->userId;
            processLogout(ctx);
        }
    }

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/chat");
    WS_PATH_LIST_END
};

/* ================= main ================= */
// 主函数
// 处理过程：配置 Windows 控制台编码，设置日志级别，启动 Drogon HTTP/WebSocket
// 服务器 返回值：int - 程序退出代码

int main() {
    std::filesystem::path exeDir;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    exeDir = std::filesystem::path(path).parent_path();
#endif

    // 拼接出 dist 的绝对路径
    std::filesystem::path distPath = exeDir / "dist";
    auto spaResp = drogon::HttpResponse::newFileResponse(distPath.string() +
                                                         "/index.html");
    spaResp->setStatusCode(drogon::k200OK);
    if (std::filesystem::exists(distPath)) {
        LOG_INFO << "前端静态资源目录定位成功: " << distPath.string();
    } else {
        LOG_ERROR << "未找到前端资源目录! 请确保它在: " << distPath.string();
    }

    drogon::app().setLogLevel(trantor::Logger::kInfo);
    LOG_INFO << "服务器启动于 127.0.0.1:9001";
    LOG_INFO << "请使用浏览器访问 127.0.0.1:9001";

    drogon::app()
        .setDocumentRoot(distPath.string())
        .setHomePage("index.html")
        .addListener("0.0.0.0", 9001)
        .setCustom404Page(spaResp, false)
        .setThreadNum(4)
        .setIdleConnectionTimeout(60)
        .run();
}