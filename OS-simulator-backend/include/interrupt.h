#pragma once
#include "./headfile.h"

#define InterruptVectorTableSize 32 //中断向量表的大小
#define Normal_Timer_Interval 100 //默认时钟中断的间隔

enum class InterruptType { //枚举中断类型
    TIMER = 0,
    DEVICE = 1, //设备采用中断触发
    SOFTWARE,
    NON_MASKABLE, //不可屏蔽中断界限
    PAGEFAULT,
    TEST,
    ERROR
};

struct Interrupt {
    InterruptType type;
    int device_id;
    int value;
    bool isBlock;
};


std::mutex iq; //中断队列锁

//中断优先级比较器
struct InterruptComparator {
    bool operator()(const Interrupt& a, const Interrupt& b) const {  
        return a.type > b.type; 
    }
};


std::priority_queue<Interrupt, std::vector<Interrupt>, InterruptComparator> InterruptQueue; //中断等待队列

std::atomic<uint16_t> valid; //valid的每个二进制位代表对应的中断类型是否有效
std::atomic<bool> timerInterruptValid; //时钟中断是否有效标志,时钟在单独的线程运行，这里仅控制时钟是否产生中断，不能控制时钟线程是否存在
std::atomic<bool> stopTimer; //直接停止时钟中断线程

typedef void (*InterruptFunc)(InterruptType, int, int); //中断处理函数指针，参数依次为中断类型，设备id，可选值
struct InterruptVector {
    InterruptFunc handler = nullptr; //中断处理函数指针
    int priority;
};

InterruptVector InterruptVectorTable[InterruptVectorTableSize]; //中断向量表

//设置中断屏蔽函数

class InterruptTool { //操作的中断的工具函数--注意仅可操作可屏蔽中断
    bool isValid(InterruptType t); //判断某类型中断是否有效
    bool setValid(InterruptType t); //设置中断有效
    bool unsetValid(InterruptType t); //设置中断无效
    bool allValid(); //打开所有可打开中断
    bool setPriority(InterruptType t, int priority); //设置中断优先级
    bool enableTimerInterrupt(); //时钟中断使能
    bool disableTimerInterrupt(); //时钟中断失能
    bool stopTimer(); //停止时钟线程-仅程序结束时使用
};

void raiseInterrupt(InterruptType t, int device_id, int value); //产生一个中断
void handleInterrupt(); //处理队列中产生的中断

void Interrupt_Init(); //中断初始化