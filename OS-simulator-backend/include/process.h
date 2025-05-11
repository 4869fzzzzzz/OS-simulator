#pragma once

#include "headfile.h"
using namespace std;

//进程状态
#define CREATING 0
#define READY 1
#define RUNNING 2
#define BLOCK 3
#define SUSPEND 4
#define DEAD 5

//阻塞状态
#define NOTBLOCK -1//不在阻塞状态
#define KEYBOARD 0
#define PRINT 1

//中断类型
#define NORMAL_SWITCH 0 //时间片轮转
#define PREEMPTION_SWITCH 1 //抢占式中断
#define MIDTERM_SWITCH_OUT 2
#define MIDTERM_SWITCH_IN 3//中期调度
#define KEYBOARD_SWITCH 4
#define PRINT_SWITCH 5 //I/O中断
#define FILE_SWITCH_READ 6//文件读中断
#define FILE_SWITCH_WRITE 7//文件写中断
#define KEYBOARD_SWITCH_OVER 8//输入结束
#define PRINT_SWITCH_OVER 9//输出结束
#define FILE_SWITCH_OVER 10//文件写结束
//ADD
#define MAX_APPLY_TIME 10 //最大申请时间

extern std::mutex ready_list_mutex;


typedef struct process_struct {
    // 基本属性
    unsigned short uid;
    int pid;
    int state;
    int cpuState;
    int prority;

    // 时间相关 - 将需要原子操作的变量改为 atomic
    int alltime;
    int cputime;
    std::atomic<int> cpuStartTime;
    std::atomic<int> keyboardStartTime;
    std::atomic<int> printStartTime;
    std::atomic<int> filewriteStartTime;
    int createtime;
    double RR;

    // 内存相关
    int task_size;
    bool is_apply;
    int address;
    int blocktype;

    // 文件相关
    std::string fs;
    std::string fsState;
    std::string content;

    // 程序相关
    std::list<std::string> program;
    std::atomic<int> current_instruction_time;
    std::queue<std::string> instructions;
    std::atomic<int> apply_time;

    // 默认构造函数
    process_struct() : 
        uid(0), pid(-1), state(CREATING), cpuState(0), prority(0),
        alltime(0), cputime(0), 
        cpuStartTime(0), keyboardStartTime(0), printStartTime(0), filewriteStartTime(0),
        createtime(0), RR(0.0),
        task_size(0), is_apply(false), address(0), blocktype(NOTBLOCK),
        current_instruction_time(0), apply_time(0) {}

    // 复制构造函数
    process_struct(const process_struct& other) : 
        uid(other.uid), pid(other.pid), state(other.state),
        cpuState(other.cpuState), prority(other.prority),
        alltime(other.alltime), cputime(other.cputime),
        cpuStartTime(other.cpuStartTime.load()),
        keyboardStartTime(other.keyboardStartTime.load()),
        printStartTime(other.printStartTime.load()),
        filewriteStartTime(other.filewriteStartTime.load()),
        createtime(other.createtime), RR(other.RR),
        task_size(other.task_size), is_apply(other.is_apply),
        address(other.address), blocktype(other.blocktype),
        fs(other.fs), fsState(other.fsState), content(other.content),
        program(other.program),
        current_instruction_time(other.current_instruction_time.load()),
        instructions(other.instructions),
        apply_time(other.apply_time.load()) {}

    // 移动构造函数
    process_struct(process_struct&& other) noexcept : 
        uid(std::exchange(other.uid, 0)),
        pid(std::exchange(other.pid, -1)),
        state(std::exchange(other.state, CREATING)),
        cpuState(std::exchange(other.cpuState, 0)),
        prority(std::exchange(other.prority, 0)),
        alltime(std::exchange(other.alltime, 0)),
        cputime(std::exchange(other.cputime, 0)),
        cpuStartTime(other.cpuStartTime.load()),
        keyboardStartTime(other.keyboardStartTime.load()),
        printStartTime(other.printStartTime.load()),
        filewriteStartTime(other.filewriteStartTime.load()),
        createtime(std::exchange(other.createtime, 0)),
        RR(std::exchange(other.RR, 0.0)),
        task_size(std::exchange(other.task_size, 0)),
        is_apply(std::exchange(other.is_apply, false)),
        address(std::exchange(other.address, 0)),
        blocktype(std::exchange(other.blocktype, NOTBLOCK)),
        fs(std::move(other.fs)),
        fsState(std::move(other.fsState)),
        content(std::move(other.content)),
        program(std::move(other.program)),
        current_instruction_time(other.current_instruction_time.load()),
        instructions(std::move(other.instructions)),
        apply_time(other.apply_time.load()) {}

    // 工具函数
    bool has_instruction() const {
        return !instructions.empty();
    }
    
    std::string get_current_instruction() {
        if (!instructions.empty()) {
            return instructions.front();
        }
        return "";
    }
} PCB;
typedef struct mutexInfo {
	bool isBusy;
	list<PCB> waitForFileList;//等待文件队列
};//文件互斥锁 读写共用


extern list<PCB> PCBList;//总进程队列
extern list<PCB> readyList;//就绪队列
extern list<PCB> blockList;//阻塞队列
extern list<PCB> suspendList;//挂起队列

extern list<PCB> readyList0;//就绪队列1
extern list<PCB> readyList1;//就绪队列2

//阻塞状态队列
extern list<PCB> waitForKeyBoardList;//等待键盘队列
extern list<PCB> waitForPrintList;//等待打印机队列

void applyForResource(PCB& p);//进程在创建态申请资源

PCB create(string filepath, string filename);
void ready(PCB& p);//就绪原语
void block(PCB& p);//阻塞原语
void stop(PCB& p);//结束原语
void suspend(PCB& p);//挂起原语

void LongTermScheduler(string filename);//长期调度程序
void MidTermScheduler(int inOrOut);//中期调度程序
void CPUScheduler(PCB& p);//短期调度程序

void Execute();//进程执行函数
void updateTaskState();//进程状态更新函数

void pInterrupt(PCB& p, int reason);//调用中断函数 I/O中断,进程调度,文件读写中断等
