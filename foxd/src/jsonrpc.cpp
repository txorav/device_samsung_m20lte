#include "jsonrpc.h"
#include <sstream>
#include <iomanip>

namespace foxd {

std::string json_escape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

std::string json_bool(bool b) {
    return b ? "true" : "false";
}

std::string json_int(long long v) {
    return std::to_string(v);
}

std::string json_string(const std::string& s) {
    return "\"" + json_escape(s) + "\"";
}

std::string JsonRpcResponse::to_string() const {
    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"2.0\",\"id\":" << (id.empty() ? "null" : id);
    if (is_error) {
        oss << ",\"error\":" << (error_json.empty() ? "{\"code\":-32603,\"message\":\"Internal error\"}" : error_json);
    } else {
        oss << ",\"result\":" << (result_json.empty() ? "{}" : result_json);
    }
    oss << "}\n";
    return oss.str();
}

bool JsonRpcDispatcher::parse_request(const std::string& raw, JsonRpcRequest& req) {
    req.id = "null";
    req.is_notification = true;

    // Search for "method"
    auto method_pos = raw.find("\"method\"");
    if (method_pos == std::string::npos) return false;
    auto colon_pos = raw.find(':', method_pos);
    if (colon_pos == std::string::npos) return false;
    auto quote_start = raw.find('"', colon_pos);
    if (quote_start == std::string::npos) return false;
    auto quote_end = raw.find('"', quote_start + 1);
    if (quote_end == std::string::npos) return false;
    req.method = raw.substr(quote_start + 1, quote_end - quote_start - 1);

    // Search for "id"
    auto id_pos = raw.find("\"id\"");
    if (id_pos != std::string::npos) {
        auto id_colon = raw.find(':', id_pos);
        if (id_colon != std::string::npos) {
            auto val_start = raw.find_first_not_of(" \t\r\n", id_colon + 1);
            if (val_start != std::string::npos) {
                if (raw[val_start] == '"') {
                    auto val_end = raw.find('"', val_start + 1);
                    if (val_end != std::string::npos) {
                        req.id = raw.substr(val_start, val_end - val_start + 1);
                        req.is_notification = false;
                    }
                } else {
                    auto val_end = raw.find_first_of(",} \t\r\n", val_start);
                    req.id = raw.substr(val_start, (val_end == std::string::npos) ? val_end : val_end - val_start);
                    req.is_notification = false;
                }
            }
        }
    }

    // Search for "params"
    auto params_pos = raw.find("\"params\"");
    if (params_pos != std::string::npos) {
        auto p_colon = raw.find(':', params_pos);
        if (p_colon != std::string::npos) {
            auto p_start = raw.find_first_not_of(" \t\r\n", p_colon + 1);
            if (p_start != std::string::npos) {
                if (raw[p_start] == '{' || raw[p_start] == '[') {
                    char open_char = raw[p_start];
                    char close_char = (open_char == '{') ? '}' : ']';
                    int depth = 0;
                    size_t p_end = p_start;
                    for (; p_end < raw.length(); ++p_end) {
                        if (raw[p_end] == open_char) depth++;
                        else if (raw[p_end] == close_char) {
                            depth--;
                            if (depth == 0) {
                                req.params_raw = raw.substr(p_start, p_end - p_start + 1);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    return !req.method.empty();
}

void JsonRpcDispatcher::register_method(const std::string& method, RpcMethodHandler handler) {
    methods_[method] = handler;
}

std::string JsonRpcDispatcher::dispatch(const std::string& raw_request) {
    JsonRpcRequest req;
    if (!parse_request(raw_request, req)) {
        JsonRpcResponse resp;
        resp.id = "null";
        resp.is_error = true;
        resp.error_json = "{\"code\":-32700,\"message\":\"Parse error\"}";
        return resp.to_string();
    }

    auto it = methods_.find(req.method);
    if (it == methods_.end()) {
        if (req.is_notification) return "";
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.is_error = true;
        resp.error_json = "{\"code\":-32601,\"message\":\"Method not found\"}";
        return resp.to_string();
    }

    try {
        std::string result = it->second(req.params_raw);
        if (req.is_notification) return "";
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.is_error = false;
        resp.result_json = result.empty() ? "{}" : result;
        return resp.to_string();
    } catch (const std::exception& e) {
        if (req.is_notification) return "";
        JsonRpcResponse resp;
        resp.id = req.id;
        resp.is_error = true;
        resp.error_json = "{\"code\":-32603,\"message\":\"" + json_escape(e.what()) + "\"}";
        return resp.to_string();
    }
}

std::string JsonRpcDispatcher::create_notification(const std::string& method, const std::string& params_json) {
    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"2.0\",\"method\":\"" << json_escape(method)
        << "\",\"params\":" << (params_json.empty() ? "{}" : params_json) << "}\n";
    return oss.str();
}

} // namespace foxd
