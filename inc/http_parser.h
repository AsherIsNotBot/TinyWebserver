#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <string>
#include <unordered_map>
#include <sstream>
#include "utils.h"

using namespace std;

// 存储解析后的 http 请求信息
struct HttpRequest
{
    string method;                         // 请求方法
    string url;                            // 请求地址
    string version;                        // http协议版本
    unordered_map<string, string> headers; // 存储不同首部字段名
    string body;                           // 请求数据
    bool keepAlive = false;                // 长连接
};

class HttpParser
{
public:
    // 解析http报文
    static int parse(const string &data, size_t &consumed, HttpRequest &req);
};

#endif
