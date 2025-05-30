#pragma once


#include "./headfile.h"
#include "./memory.h"
#include "./filesystem.h"

using namespace std;


//进程状态
#define CREATING 0
#define READY 1
#define RUNNING 2
#define BLOCK 3
#define SUSPEND 4
#define DEAD 5

//阻塞状态
#define NOTBLOCK -1 //不在阻塞状态
#define KEYBOARDB 0
#define PRINTB 1
#define SYSTEMB 2 //系统进行阻塞
#define OTHERB 3
#define FILEB 4
#define USERB 5
#define DEVICEB 6

#define MAX_APPLY_TIME 10


#define SCHED_FCFS 0                         
#define SCHED_PRO 2               
#define SCHED_RRP 3


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

//各个进程队列的最大值
#define MAX_PCB_SIZE 1024
#define MAX_SPCB_SIZE 512
#define MAX_BPCB_SIZE 256
#define MAX_RPCB_SIZE 128


const int MAX_SUSPEND_TIME = 100;
const int MAX_BLOCK_TIME = 100;  // 最大阻塞时长阈值



extern std::mutex ready_list_mutex;
extern std::mutex blockList_mutex;
extern std::mutex suspendList_mutex;
extern std::mutex prePCBList_mutex;

typedef struct process_struct {
	int pid;//进程标识
	int state;//标识进程的状态
	int cpuState;//定义进程所处的特权状态
	int priority;//进程优先级
	int cpu_num;//即将占用的cpu编号

	int cputime;//进程所需CPU的时间

	int cpuStartTime;//进程开始占用CPU的时间
	int keyboardStartTime;//进程开始占用键盘输入的时间
	int printStartTime;//进程开始占用输出的时间
	int filewriteStartTime;//进程开始占用文件写入的时间
	int createtime;//进程创建时间 到达时间

	int suspend_time;  // 挂起开始时间
	double RR;//进程的响应比 （等待时间+服务时间）/服务时间

	int blocktype;//进程阻塞的类型
	int start_block_time;//开始阻塞时间
	int need_block_time;//阻塞所需时间

	string fs;//正在访问的文件信息
	string fsState;//访问文件的方式
	string content;//写入文件的内容

	int task_size;//占用内存大小
	bool is_apply;//是否分配了内存
	v_address address;//进程在内存中的起始虚拟地址
	v_address next_v;//进程即将读取的字符虚拟地址
	string position;//进程文件的路径

	int apply_time;
	int current_instruction_time;

	string instruction;//程序段顺序执行命令

	bool operator==(const process_struct& other) const {
		return pid == other.pid; // 假设 PID 是唯一的标识
	}
	bool has_instruction(){
		if(instruction.empty())
			return false;
		else
			return true;
	}
	string get_current_instruction(){
		return instruction;
	}
}PCB;



typedef struct prePCB{
	std::string path;
	std::string filename;
}pPCB;


extern list<PCB> PCBList;//总进程队列
extern list<PCB> readyList0;//就绪队列
extern list<PCB> readyList1;
extern list<PCB> blockList;//阻塞队列
extern list<PCB> suspendList;//挂起队列



//阻塞状态队列
extern list<PCB> waitForKeyBoardList;//等待键盘队列
extern list<PCB> waitForPrintList;//等待打印机队列

extern list<pPCB> prePCBList;//待创建PCB进程队列


void applyForResource(PCB& p);//进程在创建态申请资源

PCB create(const string& path, const string& filename);
void ready(PCB& p);//就绪原语
void block(PCB& p);//阻塞原语
void stop(PCB& p);//结束原语
void suspend(PCB& p);//挂起原语

void LongTermScheduler(string path, string filename);//长期调度程序
void MidTermScheduler(int inOrOut, PCB& p);//中期调度程序
void CPUScheduler(PCB& p, int cpu);//短期调度程序

void execute();//进程执行函数
void updateTaskState();//进程状态更新函数

//zf
void CreatePCB();//给待创建进程列表创建pcb
void AllocateMemoryForPCB();//给挂起队列中的pcb分配内存，使其转到就绪队列
void CheckBlockList();//检查阻塞队列，主要是唤醒一些使用设备结束的进程
void MidStageScheduler();//中期调度程序
void shortScheduler();//短期调度

void pro_sche();//优先级调度排序
void RRP_sche();//响应比调度排序
void FILE_delete(PCB& p);
void Print_delete(PCB& p);
void Keyboard_delete(PCB& p);
void removePCBFromQueue(PCB* current_pcb);


//UI数据交换

// CPU状态信息
struct CPUStatusForUI {
    int cpu_id;
    std::string current_instruction;
    int remaining_time;
    int running_pid;
    //int running_time;

    AIGC_JSON_HELPER(cpu_id, current_instruction, remaining_time, running_pid)
    AIGC_JSON_HELPER_RENAME("id", "instruction", "remaining", "pid")

};

// 进程表项
struct ProcessTableItemForUI {
    std::string name;         // 进程名称
    int pid;                  // 进程ID
    std::string state;        // 进程状态
    std::string user;         // 用户名
    int cpu_num;             // CPU编号
    size_t memory_size;      // 内存占用
    std::string description;  // 描述信息

    AIGC_JSON_HELPER(name, pid, state, user, cpu_num, memory_size, description)
    AIGC_JSON_HELPER_RENAME("name", "pid", "state", "user", "cpu", "memory", "desc")
};

// 进程系统总览
struct ProcessOverviewForUI {
    int total_process;      // 总进程数
    int running_process;    // 运行进程数
    int blocked_process;    // 阻塞进程数
    std::vector<CPUStatusForUI> cpu_status;  // CPU状态

    AIGC_JSON_HELPER(total_process, running_process, blocked_process, cpu_status)
    AIGC_JSON_HELPER_RENAME("total", "running", "blocked", "cpus")
};

// 完整的进程系统状态
struct ProcessSystemStatusForUI {
    ProcessOverviewForUI overview;
    std::vector<ProcessTableItemForUI> process_table;

    AIGC_JSON_HELPER(overview, process_table)
    AIGC_JSON_HELPER_RENAME("overview", "processes")
};

class ProcessStatusManager {
public:
    ProcessSystemStatusForUI current_status;
    std::atomic<bool> need_update{true};

    ProcessStatusManager() = default;
    
    void update() {
        if (!need_update) {
            return;
        }
        updateOverview();
        updateProcessTable();
        updateCPUStatus();
        need_update = false;
    }
    
    const ProcessSystemStatusForUI& getCurrentStatus() const {
        return current_status;
    }
    
    void markForUpdate() { 
        need_update = true; 
    }

    // 更新总览信息
    void updateOverview();
    // 更新进程表
    void updateProcessTable();
    // 更新CPU状态
    void updateCPUStatus();
    // 获取进程状态描述
    std::string getProcessStateString(int state);
};