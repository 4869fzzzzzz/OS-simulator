#pragma once

// 1. C++ 标准库核心组件
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

// 2. C++ 标准库容器
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <stack>
#include <list>
#include <unordered_map>

// 3. C++ 标准库算法和工具
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

// 4. C 标准库组件
#include <cstdint>
#include <ctime>
#include <cstdlib>

// 5. Windows 系统相关（注意顺序）
#ifdef _WIN32
#include <winsock2.h>  // 必须在 windows.h 之前
#include <windows.h>
#endif

// 6. 第三方库
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "AIGCJson.hpp"

#define SOCKETBEGIN 0
using namespace aigc;

