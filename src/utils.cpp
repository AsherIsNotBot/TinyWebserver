#include "utils.h"
#include "log.h"

#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>

string toLower(string s)
{
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
              { return tolower(c); });
    return s;
}

// 去除字符串首尾的空格与指表符
string thrim(const string &s)
{ // 为什么不能把下面的两个相同的函数拿来用
    size_t b = s.find_first_not_of(" \t");
    if (b == string::npos)
        return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}
bool readFile(const string &path, string &out)
{
    ifstream ifs(path, ios::binary); // 以二进制模式打开文件
    if (!ifs)
        return false;
    ostringstream oss;
    oss << ifs.rdbuf(); // 获取文件底层原始数据缓冲区，输入字符串流中
    out = oss.str();
    return true;
}
string mimeType(const string &path)
{
    size_t dot = path.find_last_of('.');
    if (dot == string::npos)
        return "application/octet-stream";
    string ext = path.substr(dot + 1);
    ext = toLower(ext);

    if (ext == "html" || ext == "htm")
        return "text/html; charset=utf-8";
    if (ext == "css")
        return "text/css";
    if (ext == "js")
        return "application/javascript";
    if (ext == "json")
        return "application/json";
    if (ext == "png")
        return "image/png";
    if (ext == "gif")
        return "image/gif";
    if (ext == "txt")
        return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

void setnonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        LOG_ERROR("fcntl getfile error");
        exit(1);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        LOG_ERROR("fcntl setfile error");
        exit(1);
    }
}
