#include "httprequest.h"

const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML{
            "/index", "/register", "/login",
             "/welcome", "/video", "/picture", };

const std::unordered_map<std::string, int> HttpRequest::DEFAULT_HTML_TAG {
            {"/register.html", 0}, {"/login.html", 1},  };

void HttpRequest::Init() {
    state_ = REQUEST_LINE;
    method_ = path_ = version_ = body_ = "";
    header_.clear();
    post_.clear();
}

bool HttpRequest::IsKeepAlive() const {
    if (header_.count("Connection")) {
        return header_.find("Connection")->second == "keep-alive" &&
                version_ == "1.1";
    }
    return false;
}

bool HttpRequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";
    
    if (buff.ReadableBytes() <= 0) return false;

    while (buff.ReadableBytes() && state_ != FINISH) {
        // 读取一行并去除尾部回车换行符
        const char* lineEnd = std::search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
        std::string line(buff.Peek(), lineEnd);

        switch (state_) {
            case REQUEST_LINE:
                if (!ParseRequestLine_(line)) return false;
                ParsePath_();
                break;
            case HEADERS:
                ParseHeader_(line);
                if (buff.ReadableBytes() <= 2) state_ = FINISH;
                break;
            case BODY:
                ParseBody_(line);
                break;
            default:
                break;
        }
        if (lineEnd == buff.BeginWrite()) break;
        buff.RetrieveUntil(lineEnd + 2);
    }
    LOG_DEBUG("[%s] [%s] [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

// 解析路径
void HttpRequest::ParsePath_() {
    if (path_ == "" || path_ == "/") {
        path_ = "/index.html";
        return;
    }
    
    for (auto& item : DEFAULT_HTML) {
        if (path_ == item) {
            path_ += ".html";
            break;
        }
    }
}

bool HttpRequest::ParseRequestLine_(const std::string& line) {
    // 正则表达式模式串
    static const std::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    std::smatch subMatch;

    if (std::regex_match(line, subMatch, patten)) {
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];
        state_ = HEADERS;
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

void HttpRequest::ParseHeader_(const std::string& line) {
    static const std::regex patten("^([^:]*): ?(.*)$");
    std::smatch subMatch;
    if (std::regex_match(line, subMatch, patten)) {
        header_[subMatch[1]] = subMatch[2];
    } else {
        state_ = BODY;
    }
}

void HttpRequest::ParseBody_(const std::string& line) {
    body_ = line;
    ParsePost_();
    state_ = FINISH;
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

int HttpRequest::ConverHex(char ch) {
    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}

void HttpRequest::ParsePost_() {
    if (method_ != "POST" || header_["Content-Type"] != "application/x-www-form-urlencoded")
        return;

    ParseFromUrlencoded_();

    if (DEFAULT_HTML_TAG.count(path_)) {
        int tag = DEFAULT_HTML_TAG.find(path_)->second;
        LOG_DEBUG("Tag: %d", tag);
        bool isLogin = tag == 1;
        if (UserVerify(post_["username"], post_["passoword"], isLogin)) {
            path_ = "/welcome.html";
        } else {
            path_ = "/error.html";
        }
    }
}

void HttpRequest::ParseFromUrlencoded_() {
    if (body_.empty()) return;

    std::string key, value;
    int num = 0;
    int n = body_.size();
    int i, j;

    for (i = 0, j = 0; i < n; i++) {
        char c = body_[i];
        switch (c) {
            case '=':
                key = body_.substr(j, i - j);
                j = i + 1;
                break;
            case '+':
                body_[i] = ' ';
                break;
            case '%':
                num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
                body_[i + 2] = num % 10 + '0';
                body_[i + 1] = num / 10 + '0';
                i += 2;
                break;
            case '&':
                value = body_.substr(j, i - j);
                j = i + 1;
                post_[key] = value;
                LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
                break;
            default:
                break;
        }
        if (!post_.count(key) && j < i) {
            value = body_.substr(j, i - j);
            post_[key] = value;
        }
    }
}


bool HttpRequest::UserVerify(const std::string &name, const std::string &pwd, bool isLogin) {
    if (name.empty() || pwd.empty()) return false;

    LOG_INFO("Verify name: %s pwd: %s", name.c_str(), pwd.c_str());

    bool flag = false;

    try {
        SqlConnRAII connWrapper;
        sql::Connection* conn = connWrapper.get();

        if (connWrapper.get() == nullptr || connWrapper.get()->isClosed()) {
            LOG_ERROR("Database connection is invalid");
            return false;
        }

        std::unique_ptr<sql::PreparedStatement> pstmt;
        std::unique_ptr<sql::ResultSet> res;
        
        if(isLogin) {
            // 登录验证：查询用户名和密码
            pstmt.reset(conn->prepareStatement(
                "SELECT username, password FROM user WHERE username = ? LIMIT 1"));
            LOG_DEBUG("Executing query: SELECT username, password FROM user WHERE username = %s LIMIT 1", name.c_str());
        } else {
            // 注册检查：只查询用户名是否存在
            pstmt.reset(conn->prepareStatement(
                "SELECT username FROM user WHERE username = ? LIMIT 1"));
            LOG_DEBUG("Executing query: SELECT username FROM user WHERE username = %s LIMIT 1", name.c_str());
        }

        pstmt->setString(1, name);
        
        res.reset(pstmt->executeQuery());

        if (res->next()) {
            // 用户存在
            if (isLogin) {
                // 登录验证：检查密码
                std::string storedPassword = res->getString("password");
                if (pwd == storedPassword) {
                    flag = true;
                    LOG_DEBUG("Login successful for user: %s", name.c_str());
                } else {
                    flag = false;
                    LOG_INFO("Password error for user: %s", name.c_str());
                }
            } else {
                // 注册时用户名已存在
                flag = false;
                LOG_INFO("Username already used: %s", name.c_str());
            }
        } else {
            // 用户不存在
            if(isLogin) {
                // 登录时用户不存在
                flag = false;
                LOG_INFO("User not found: %s", name.c_str());
            } else {
                // 注册时用户不存在，可以注册
                flag = true;
            }
        }

        if (!isLogin && flag) {
            LOG_DEBUG("Registering new user: %s", name.c_str());
            
            std::unique_ptr<sql::PreparedStatement> insertStmt(
                conn->prepareStatement(
                    "INSERT INTO user(username, password) VALUES(?, ?)"));
            
            insertStmt->setString(1, name);
            insertStmt->setString(2, pwd);
            
            LOG_DEBUG("Executing insert: INSERT INTO user(username, password) VALUES(%s, %s)", name.c_str(), pwd.c_str());

            int affectedRows = insertStmt->executeUpdate();
            if(affectedRows > 0) {
                flag = true;
                LOG_DEBUG("User registration successful: %s", name.c_str());
            } else {
                flag = false;
                LOG_ERROR("User registration failed: %s", name.c_str());
            }
        }

        LOG_DEBUG("UserVerify %s for user: %s", 
                 flag ? "successful" : "failed", name.c_str());
        return flag;

    } catch (const sql::SQLException& e) {
        LOG_ERROR("SQL Exception in UserVerify: %s (MySQL error code: %d, SQLState: %s)",
                 e.what(), e.getErrorCode(), e.getSQLState().c_str());
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in UserVerify: %s", e.what());
        return false;
    }
}

std::string HttpRequest::path() const{
    return path_;
}

std::string& HttpRequest::path() {
    return path_;
}

std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::version() const {
    return version_;
}

std::string HttpRequest::GetPost(const std::string& key) const {
    assert(!key.empty());
    if (post_.count(key)) {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if (post_.count(key)) {
        return post_.find(key)->second;
    }
    return "";
}