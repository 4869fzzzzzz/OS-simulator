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


#define InterruptVectorTableSize 32 //中断向量表的大小
#define Normal_Timer_Interval 100 //默认时钟中断的间隔


extern std::atomic<uint16_t> valid; //valid的每个二进制位代表对应的中断类型是否有效
extern std::atomic<bool> timerInterruptValid; //时钟中断是否有效标志,时钟在单独的线程运行，这里仅控制时钟是否产生中断，不能控制时钟线程是否存在
extern std::atomic<bool> stopTimerFlag; //直接停止时钟中断线程
extern std::atomic<bool> handleFlag;
extern std::atomic<long long> time_cnt;
extern std::thread th[2];
extern std::atomic<int> interrupt_handling_cpus;  // 记录正在处理中断的CPU数量

struct process_struct;
typedef process_struct PCB;



enum class InterruptType { //枚举中断类型-类型的数值用于标注在中断向量表中的位置
    TIMER = 0,
    DEVICE = 1, //设备采用中断触发
    SOFTWARE,
    SNAPSHOT,
    NON_MASKABLE, //不可屏蔽中断界限
    PAGEFAULT,
    TEST,
    MERROR
};
//中断
struct Interrupt {
    InterruptType type;
    int value1;
    int value2;
    std::string value3;
    std::shared_ptr<int> value4;  
    int value5;
    long long timecount;

    // 构造函数
    Interrupt(InterruptType tp, int v1, int v2, std::string v3, int* v4, int v5) 
        : type(tp), 
          value1(v1), 
          value2(v2), 
          value3(v3),
          value4(v4 ? std::make_shared<int>(*v4) : nullptr),  // 创建智能指针
          value5(v5),
          timecount(time_cnt.load()) {}


    // 拷贝构造函数
     Interrupt(const Interrupt& other) = default;
    // 赋值运算符
    Interrupt& operator=(const Interrupt& other) {
        if (this != &other) {
            type = other.type;
            value1 = other.value1;
            value2 = other.value2;
            value3 = other.value3;
            value4 = other.value4;
            value5 = other.value5;
            timecount = other.timecount;
        }
        return *this;
    }

    // 移动构造函数
    Interrupt(Interrupt&& other) noexcept :
        type(other.type),
        value1(other.value1),
        value2(other.value2),
        value3(std::move(other.value3)),
        value4(std::move(other.value4)),
        value5(other.value5),
        timecount(other.timecount) {}

    // 移动赋值运算符
    Interrupt& operator=(Interrupt&& other) noexcept {
        if (this != &other) {
            type = other.type;
            value1 = other.value1;
            value2 = other.value2;
            value3 = std::move(other.value3);
            value4 = std::move(other.value4);
            value5 = other.value5;
            timecount = other.timecount;
        }
        return *this;
    }
};

typedef void (*InterruptFunc)(int, int, std::string, int*, int); //中断处理函数指针，参数依次为中断类型，设备id，可选值

struct InterruptVector {
    InterruptFunc handler = nullptr; //中断处理函数指针
    int priority;//中断处理优先级
    InterruptVector(InterruptFunc mhandler,int pri){
        handler = mhandler;
        priority = pri;
    }
    InterruptVector() : handler(nullptr), priority(0) {} 
    

};

extern InterruptVector InterruptVectorTable[InterruptVectorTableSize]; //中断向量表

extern std::mutex iq; //中断队列锁

//中断优先级比较器
struct InterruptComparator {
    bool operator()(const Interrupt& a, const Interrupt& b) const {  
        return InterruptVectorTable[static_cast<int>(a.type)].priority < InterruptVectorTable[static_cast<int>(b.type)].priority;
    }//数字越大优先级越高
};

extern std::priority_queue<Interrupt, std::vector<Interrupt>, InterruptComparator> InterruptQueue; //中断等待队列

//设置中断屏蔽函数

class InterruptTool { //操作的中断的工具函数--注意仅可操作可屏蔽中断
public:
    static bool isValid(InterruptType t); //判断某类型中断是否有效
    static bool setValid(InterruptType t); //设置中断有效
    static bool unsetValid(InterruptType t); //设置中断无效
    static bool allValid(); //打开所有可打开中断
    static bool setPriority(InterruptType t, int mpriority); //设置中断优先级
    static bool enableTimerInterrupt(); //时钟中断使能
    static bool disableTimerInterrupt(); //时钟中断失能
    static bool stopTimer(); //停止时钟线程-仅程序结束时使用
};

//中断函数
void raiseInterrupt(InterruptType t, int v1, int v2, std::string v3,int* v4,int v5); //产生一个中断
void handleInterrupt(); //处理队列中产生的中断

void delay(int timeout_ms);
void TimeThread(int interval);
char* timeToChar(time_t time);
struct tm* timeToStruct(time_t time);

void noHandle(int v1,int v2,std::string v3,int* v4, int v5);
void errorHandle(int v1,int v2,std::string v3,int* v4, int v5);

time_t get_startSysTime();
time_t get_nowSysTime();



void Interrupt_Init(); //中断初始化

void PrintInterruptList();
void PrintInterruptTable();
