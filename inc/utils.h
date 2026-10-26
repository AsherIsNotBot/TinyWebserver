#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <vector>
#include <queue>
#include <string>
#include <algorithm>
#include <sstream>
#include <fstream>

using namespace std;

string toLower(string s);

// 去除字符串首尾的空格与指表符
string thrim(const string &s);

bool readFile(const string &path, string &out);

string mimeType(const string &path);

void setnonblock(int fd);

#endif
