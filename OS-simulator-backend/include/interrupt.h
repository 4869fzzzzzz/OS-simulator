#pragma once
#include "./headfile.h"
#include "./socket.h"
#include "./client.h"
#include "./process.h"
#include "./memory.h"
#include "./device.h"
#include "./filesystem.h"


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
//新的中断参数 
struct Interrupt {
    InterruptType type;
    int value1;
    int value2;
    std::string value3;
    int* value4;
    int value5;
    long long timecount;//产生中断时间
    Interrupt(InterruptType tp,int v1,int v2,std::string v3,int* v4,int v5){
        type=tp;
        value1=v1;
        value2=v2;
        value3=v3;
        value4=v4;
        value5=v5;
        timecount=time_cnt.load();
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
    bool isValid(InterruptType t); //判断某类型中断是否有效
    bool setValid(InterruptType t); //设置中断有效
    bool unsetValid(InterruptType t); //设置中断无效
    bool allValid(); //打开所有可打开中断
    bool setPriority(InterruptType t, int mpriority); //设置中断优先级
    bool enableTimerInterrupt(); //时钟中断使能
    bool disableTimerInterrupt(); //时钟中断失能
    bool stopTimer(); //停止时钟线程-仅程序结束时使用
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
//主程序与CPU
struct CPU {
    int id;                     // CPU ID
    bool busy;                  // CPU 状态
    PCB* running_process;       // 当前运行的进程
    std::atomic<bool> running;  // CPU 运行状态
    
    CPU(int cpu_id) : id(cpu_id), busy(false), running_process(nullptr), running(false) {}

};
extern CPU cpu0,cpu1;

bool RUN(std::string cmd,PCB* current_pcb);//运行一条指令
void CmdSplit(const std::string& cmd,std::vector<std::string>& scmd);//划分指令
bool handleClientCmd(std::string cmd, std::string& result);
void cpu_worker(CPU& cpu);

void incrementInterruptCount(InterruptType type);


//UI数据交换
class TimerData{
public:
    std::string startSysTime;
    std::string nowSysTime;
    long long time_cnt;
    AIGC_JSON_HELPER(startSysTime,nowSysTime,time_cnt)
};
// 中断向量表项数据结构
class InterruptVectorItem {
    public:
        std::string type;          // 中断类型名称
        bool enabled;              // 中断是否启用
        int priority;             // 中断优先级
        std::string handler_name;  // 处理函数名称
        int trigger_count;        // 触发次数
        AIGC_JSON_HELPER(type, enabled, priority, handler_name, trigger_count)
};
    
    // 中断队列项数据结构
class InterruptQueueItem {
    public:
        int index;               // 序号
        std::string type;        // 中断类型
        std::string device;      // 设备类型
        long long raise_time;    // 中断产生时间
        AIGC_JSON_HELPER(index, type, device, raise_time)
};
    
    // 中断系统数据交换类
class InterruptSystemData {
    public:
        // 总览信息
        struct Overview {
            bool timer_enabled;          // 时钟是否开启
            int timer_interval;          // 时钟中断间隔
            std::string current_time;    // 当前时钟时间
            int total_interrupts;        // 产生中断总数
            AIGC_JSON_HELPER(timer_enabled, timer_interval, current_time, total_interrupts)
        } overview;
    
        // 中断向量表
        std::vector<InterruptVectorItem> vector_table;
        
        // 中断处理队列
        std::vector<InterruptQueueItem> interrupt_queue;
    
        AIGC_JSON_HELPER(overview, vector_table, interrupt_queue)
    
        // 更新数据方法
        void update() {
            // 更新总览信息
            overview.timer_enabled = timerInterruptValid.load();
            overview.timer_interval = Normal_Timer_Interval;
            overview.current_time = timeToChar(get_nowSysTime());
            overview.total_interrupts = calculateTotalInterrupts();
    
            // 更新中断向量表
            vector_table.clear();
            for (int i = 0; i < InterruptVectorTableSize; i++) {
                if (InterruptVectorTable[i].handler != nullptr) {
                    InterruptVectorItem item;
                    item.type = getInterruptTypeName(static_cast<InterruptType>(i));
                    item.enabled = (valid.load() & (1 << i));
                    item.priority = InterruptVectorTable[i].priority;
                    item.handler_name = getHandlerName(InterruptVectorTable[i].handler);
                    item.trigger_count = getTriggerCount(static_cast<InterruptType>(i));
                    vector_table.push_back(item);
                }
            }
    
            // 更新中断队列
            interrupt_queue.clear();
            int index = 0;
            std::lock_guard<std::mutex> lock(iq);
            auto tempQueue = InterruptQueue;
            while (!tempQueue.empty()) {
                auto interrupt = tempQueue.top();
                InterruptQueueItem item;
                item.index = index++;
                item.type = getInterruptTypeName(interrupt.type);
                item.device = getDeviceType(interrupt.value1);
                item.raise_time = interrupt.timecount;
                interrupt_queue.push_back(item);
                tempQueue.pop();
            }
        }
    
    private:
        std::string getInterruptTypeName(InterruptType type);
        std::string getHandlerName(InterruptFunc handler);
        std::string getDeviceType(int device_id);
        int calculateTotalInterrupts();
        int getTriggerCount(InterruptType type);
};

class SystemSnapshot {
public:
    TimerData timer;
    ProcessSystemStatusForUI process;
    InterruptSystemData interrupt;

    AIGC_JSON_HELPER(timer, process, interrupt)
    AIGC_JSON_HELPER_RENAME("timer", "process", "interrupt")
};
void snapshotSend(int v1,int v2,std::string v3,int* v4, int v5);