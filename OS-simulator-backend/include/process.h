#pragma once


#include<stdio.h>
#include<stdlib.h>
#include<string>
#include<math.h>
#include<algorithm>
#include<iostream>
#include<fstream>
#include<vector>
#include<queue>
#include<list>
#include<map>
#include<time.h>
#include<sstream>
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


extern std::mutex ready_list_mutex;


typedef struct process_struct {
	unsigned short uid;//用户标识
	int pid;//进程标识
	int state;//标识进程的状态
	int cpuState;//定义进程所处的特权状态
	int prority;//进程优先级

	int alltime;//进程需要CPU总时间
	int cputime;//进程占用CPU的时间
	int cpuStartTime;//进程开始占用CPU的时间
	int keyboardStartTime;//进程开始占用键盘输入的时间
	int printStartTime;//进程开始占用输出的时间
	int filewriteStartTime;//进程开始占用文件写入的时间
	int createtime;//进程创建时间 到达时间
	double RR;//进程的响应比 （等待时间+服务时间）/服务时间

	int task_size;//占用内存块数
	bool is_apply;//是否分配了内存
	int address;//保存分配的内存虚拟地址

	int blocktype;//进程阻塞的类型

	string fs;//正在访问的文件信息
	string fsState;//访问文件的方式
	string content;//写入文件的内容


	list<string> program;//程序段顺序执行命令

	//ADD，新的指令存储队列和函数
	std::atomic<int> current_instruction_time;
    std::queue<std::string> instructions;
    
    bool has_instruction() const {
        return !instructions.empty();
    }
    
    std::string get_current_instruction() {
        if (!instructions.empty()) {
            std::string instr = instructions.front();
            
            return instr;
        }
        return "";
    }
}PCB;

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
