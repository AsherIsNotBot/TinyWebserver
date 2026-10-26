#include "http_parser.h"

// 解析http报文
int HttpParser::parse(const string &data, size_t &consumed, HttpRequest &req)
{
    consumed = 0;
    // 查找 "\r\n\r\n" ,这是http头部和请求体的分界线
    size_t headerEnd = data.find("\r\n\r\n");

    // 如果找不到，说明客户端的数据还没有发完，暂时不解析
    if (headerEnd == string::npos)
        return 0;

    size_t bodyStart = headerEnd + 4; // 请求体在空行后开始
    size_t lineEnd = data.find("\r\n");

    // 防止格式错误
    if (lineEnd == string::npos)
        return -1;

    string requestLine = data.substr(0, lineEnd);

    //  读取方法，URL，版本
    istringstream iss(requestLine);
    if (!(iss >> req.method >> req.url >> req.version))
    {
        return -1;
    }

    // 检验版本号
    if (req.version.rfind("HTTP/", 0) != 0)
        return -1;

    size_t cur = lineEnd + 2; // 来到第一行的首部
    while (cur < headerEnd)
    {
        size_t e = data.find("\r\n", cur);
        if (e == string::npos || e > headerEnd)
            break;

        string line = data.substr(cur, e - cur);
        size_t colon = line.find(':');

        // 拆分 Key 和 Value
        if (colon != string::npos)
        {
            // Key 统一转成 小写并去除空格
            string key = toLower(thrim(line.substr(0, colon)));
            // Value 只需去除空格
            string val = thrim(line.substr(colon + 1));
            if (!key.empty())
            {
                req.headers[key] = val;
            }
        }
        cur = e + 2; // 移动到下一行
    }

    // 处理请求体
    size_t contentLength = 0;
    auto it = req.headers.find("content-length");
    if (it != req.headers.end())
    {
        try
        {
            contentLength = stoul(it->second);
        }
        catch (...)
        {
            return -1;
        }
    }
    // 检查缓冲区收到的数据是否已经包含完整的Body
    if (data.size() < bodyStart + contentLength)
    {
        return 0;
    }
    // 提取body
    req.body = data.substr(bodyStart, contentLength);

    // 记录成功解析花费缓冲区多少字节
    consumed = bodyStart + contentLength;

    // 判断是否是长连接
    // HTTP/1.1 默认是长连接
    // HTTP/1.0 默认是短连接
    string conn;
    auto connIt = req.headers.find("connection");
    if (connIt != req.headers.end())
    {
        conn = connIt->second;
    }
    if (req.version == "HTTP/1.1")
    {
        req.keepAlive = (conn != "close");
    }
    else
    {
        req.keepAlive = (conn == "keep-alive");
    }
    return 1;
}
