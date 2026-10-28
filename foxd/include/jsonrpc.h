#ifndef FOXD_JSONRPC_H
#define FOXD_JSONRPC_H

#include <string>
#include <vector>
#include <map>
#include <functional>

namespace foxd {

struct JsonRpcRequest {
    std::string jsonrpc;
    std::string method;
    std::string params_raw;
    std::string id;
    bool is_notification = false;
};

struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    std::string id;
    std::string result_json;
    std::string error_json;
    bool is_error = false;

    std::string to_string() const;
};

using RpcMethodHandler = std::function<std::string(const std::string& params_json)>;

class JsonRpcDispatcher {
public:
    JsonRpcDispatcher() = default;

    void register_method(const std::string& method, RpcMethodHandler handler);
    std::string dispatch(const std::string& raw_request);
    static std::string create_notification(const std::string& method, const std::string& params_json);

private:
    std::map<std::string, RpcMethodHandler> methods_;
    bool parse_request(const std::string& raw, JsonRpcRequest& req);
};

// Lightweight JSON serialization helpers
std::string json_escape(const std::string& s);
std::string json_bool(bool b);
std::string json_int(long long v);
std::string json_string(const std::string& s);

} // namespace foxd

#endif // FOXD_JSONRPC_H
