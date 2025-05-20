#include "../include/process.h"
#include "../include/interrupt.h"
#include <list>
#include <string>
#include <iostream>
#include <sstream>
using namespace std;

int pid_num = 1;

// 定义全局变量
list<PCB> PCBList;    // 总进程队列
list<PCB> readyList0; // CPU0 的就绪队列
list<PCB> readyList1; // CPU1 的就绪队列
list<PCB> blockList; //阻塞队列
list<PCB> suspendList; //挂起队列

std::mutex prePCBList_mutex;
std::mutex suspendList_mutex;
std::mutex ready_list_mutex;
std::mutex blockList_mutex;

list<PCB> waitForKeyBoardList;//等待键盘队列
list<PCB> waitForPrintList;//等待打印机队列



list<PCB> cpusche0; // CPU0 的调度队列
list<PCB> cpusche1; // CPU1 的调度队列

list<pPCB> prePCBList; // 待创建PCB进程队列




//以上为暂用

void removePCBFromQueue(PCB* current_pcb) {
    // 检查进程是否为空
    if (current_pcb == nullptr) {
        std::cout << "当前进程为空，无法操作" << std::endl;
        return;
    }

    // 根据状态执行不同的操作
    switch (current_pcb->state) {
    case READY:
    {
        // 查找并从对应队列中移除
        std::lock_guard<std::mutex> lock(ready_list_mutex);
        if (current_pcb->pid != -1) { // 如果pid不为-1（表示有效进程）
            auto it = std::find_if(readyList0.begin(), readyList0.end(),
                [current_pcb](PCB& p) { return p.pid == current_pcb->pid; });
            if (it != readyList0.end()) {
                readyList0.erase(it);  // 移除该进程
                std::cout << "从CPU0的就绪队列中移除进程 " << current_pcb->pid << std::endl;
                return;
            }
        }
        if (current_pcb->pid != -1) {
            auto it = std::find_if(readyList1.begin(), readyList1.end(),
                [current_pcb](PCB& p) { return p.pid == current_pcb->pid; });
            if (it != readyList1.end()) {
                readyList1.erase(it);  // 移除该进程
                std::cout << "从CPU1的就绪队列中移除进程 " << current_pcb->pid << std::endl;
                return;
            }
        }
        ready_list_mutex.unlock();
        break;
    }
    case BLOCK:
    {
        // 查找并从阻塞队列中移除
        auto it = std::find_if(blockList.begin(), blockList.end(),
            [current_pcb](PCB& p) { return p.pid == current_pcb->pid; });
        if (it != blockList.end()) {
            blockList.erase(it);  // 移除该进程
            std::cout << "从阻塞队列中移除进程 " << current_pcb->pid << std::endl;
        }
        break;
    }
    case SUSPEND:
    {
        // 查找并从挂起队列中移除
        auto it = std::find_if(suspendList.begin(), suspendList.end(),
            [current_pcb](PCB& p) { return p.pid == current_pcb->pid; });
        if (it != suspendList.end()) {
            suspendList.erase(it);  // 移除该进程
            std::cout << "从挂起队列中移除进程 " << current_pcb->pid << std::endl;
        }
        break;
    }
    default:
        std::cout << "未知进程状态，无法处理" << std::endl;
        break;
    }
}

void pro_sche() {
    auto compareByPriorityDesc = [](const PCB& a, const PCB& b) {
        return a.priority > b.priority;
        };

    readyList0.sort(compareByPriorityDesc);
    readyList1.sort(compareByPriorityDesc);
}
void updateRRAndSortByRR(list<PCB>& readyList) {//有用
    int now = time_cnt.load();  // 获取当前系统时间

    // 更新每个 PCB 的响应比
    for (auto& pcb : readyList) {
        int serviceTime = pcb.cputime; // 如果 cputime 为 0，响应比设为 1.0
        if (pcb.cputime == 0) {
            pcb.RR = 1.0;
        }
        else {
            pcb.RR = static_cast<double>(now - pcb.createtime+serviceTime) / serviceTime;
        }
    }

    // 按响应比从高到低排序
    readyList.sort([](const PCB& a, const PCB& b) {
        return a.RR > b.RR;
        });
}

// 对全局队列排序
void RRP_sche() {
    updateRRAndSortByRR(readyList0);
    updateRRAndSortByRR(readyList1);
}


/*进程是否处在占用键盘或等待键盘事件中*/
bool Under_Keyboard(PCB& p) {
    for (list<PCB>::iterator i = waitForKeyBoardList.begin(); i != waitForKeyBoardList.end(); ++i) {
        if (i->pid == p.pid) {
            return true;
        }
    }
    return false;
};

/*进程是否处在占用键盘或等待键盘事件中*/
bool Under_Print(PCB& p) {
    for (list<PCB>::iterator i = waitForPrintList.begin(); i != waitForPrintList.end(); ++i) {
        if (i->pid == p.pid) {
            return true;
        }
    }
    return false;
};

// 就绪原语
void ready(PCB& p) {
    p.state = READY;
    auto pcb_it = std::next(PCBList.begin(), p.pid - 1);
    if(pcb_it!=PCBList.end()&&pcb_it->pid==p.pid) 
        pcb_it->state=READY;
    std::lock_guard<std::mutex> lock(ready_list_mutex);
    if (readyList0.size() <= readyList1.size()) {
        readyList0.push_back(p);
        p.cpu_num = 0;
        cout << "进程 " << p.pid << " 加入 CPU0 就绪队列" << endl;
    }
    else {
        readyList1.push_back(p);
        p.cpu_num = 1;
        cout << "进程 " << p.pid << " 加入 CPU1 就绪队列" << endl;
    }
    ready_list_mutex.unlock();
}

// 阻塞原语
void block(PCB& p) {

    p.state = BLOCK;
    auto pcb_it = std::next(PCBList.begin(), p.pid - 1);
    if(pcb_it!=PCBList.end()&&pcb_it->pid==p.pid) 
        pcb_it->state=READY;
    blockList_mutex.lock();
    blockList.push_back(p);
    blockList_mutex.unlock();
    // 从就绪队列中移除
    std::lock_guard<std::mutex> lock(ready_list_mutex);
    for (auto it = readyList0.begin(); it != readyList0.end(); ++it) {
        if (it->pid == p.pid) {
            readyList0.erase(it);
            cout << "进程 " << p.pid << " 从 CPU0 就绪队列中移除" << endl;
            break;
        }
    }
    for (auto it = readyList1.begin(); it != readyList1.end(); ++it) {
        if (it->pid == p.pid) {
            readyList1.erase(it);
            cout << "进程 " << p.pid << " 从 CPU1 就绪队列中移除" << endl;
            break;
        }
    }
    ready_list_mutex.unlock();
    
    cout << "进程 " << p.pid << " 被阻塞" << endl;
}

// 暂停原语
void stop(PCB& p) {
    p.state = DEAD;
    auto it = find(PCBList.begin(), PCBList.end(), p);
    if (it != PCBList.end()) {
        PCBList.erase(it);
        cout << "进程 " << p.pid << " 从 PCBList 中移除" << endl;
    }
    free_process_memory(p.pid);
    cout << "进程 " << p.pid << " 已终止" << endl;
}

// 挂起原语
void suspend(PCB& p) {

    p.state = SUSPEND;
    p.suspend_time = time_cnt.load();      // 记录当前时间
    auto pcb_it = std::next(PCBList.begin(), p.pid - 1);
    if(pcb_it!=PCBList.end()&&pcb_it->pid==p.pid) 
        pcb_it->state=SUSPEND;
    suspendList_mutex.lock();
    suspendList.push_back(p);
    suspendList_mutex.unlock();
    cout << "进程 " << p.pid << " 被挂起" << endl;
}


// 创建PCB
PCB create(const string& path, const string& filename) {
    PCB p;
    
    // 基本信息
    p.pid = pid_num++;
    p.state = CREATING;
    p.cpuState = 0;            // 默认用户态
    p.priority = rand() % 10;  // 随机优先级 0-9
    p.cpu_num = -1;           // 未分配CPU
    
    // 时间相关
    p.cputime = 0;
    p.cpuStartTime = -1;
    p.keyboardStartTime = -1;
    p.printStartTime = -1;
    p.filewriteStartTime = -1;
    p.createtime = time_cnt.load();
    p.suspend_time = -1;
    p.RR = 0.0;
    
    // 阻塞相关
    p.blocktype = NOTBLOCK;
    p.start_block_time = 0;
    p.need_block_time = 0;
    p.apply_time = 0;
    p.nodevice_time = -1;
    
    // 文件相关
    p.fs = "";
    p.fsState = "";
    p.content = "";
    p.position = path + "/" + filename;
    
    // 读取文件内容
    string content = fs.readFile(path,filename);
    cout << "[DEBUG] Read " << content.size() << " bytes from file " << path << "/" << filename << endl;
    
    // 内存相关
    p.task_size = content.size();
    p.is_apply = false;
    p.address = FULL;
    p.next_v = 0;
    
    // 指令相关
    p.instruction = content;  // 将文件内容存储到instruction字段
    p.current_instruction_time = 0;
    
    return p;
}


void CreatePCB()
{
    while(!prePCBList.empty()&&PCBList.size() < MAX_PCB_SIZE&&suspendList.size() < MAX_SPCB_SIZE){
        prePCBList_mutex.lock();
        //创建pcb
        PCB newProcess = create(prePCBList.front().path, prePCBList.front().filename);
        //分配虚拟地址空间
        cout<<"所需空间大小"<<newProcess.task_size<<endl;
        newProcess.address=alloc_for_process(newProcess.pid, newProcess.task_size);
        cout<<newProcess.address<<endl;

        PCBList.push_back(newProcess);
        suspendList_mutex.lock();
        suspendList.push_back(newProcess);
        suspendList_mutex.unlock();
        prePCBList.pop_front();
        prePCBList_mutex.unlock();
        std::cout<<"PCB"<<newProcess.pid<<"被添加到挂起队列"<<std::endl;
    }
}
void AllocateMemoryForPCB()
{
    while (!suspendList.empty()) {
        cout<<"尝试给挂起队列进程分配内存"<<endl;
        suspendList_mutex.lock();
        PCB p = suspendList.front();
        suspendList.pop_front();
        if(p.state==CREATING){
            cout<<p.address<<endl;
            if(page_in(p.address,p.pid)==0){
                std::cout << "进程 " << p.pid << " 分配内存成功" << std::endl;
                p.state = READY;
                ready(p);
            }
            else{
                std::cout << "进程 " << p.pid << " 分配内存失败" << std::endl;
                suspendList.push_back(p);
            }
        }
        else{
            suspendList.push_back(p);
        }
        suspendList_mutex.unlock();
    }
}

void CheckBlockList()
{
    //阻塞队列的操作有下面几类
    //1.检查阻塞类型为设备，检查其是否已成功申请到设备，
    //start_block_time为0表示正在申请忽略，
    //大于0表示已经开始使用计算当前时间减去start_block_time是否大于need_block_time,大于则表示已经使用结束，清除当前进程的指令和指令时间等，恢复到就绪队列，小于则忽略
    //小于0表示申请失败
    
    blockList_mutex.lock();
    for (auto it = blockList.begin(); it != blockList.end();) {
        PCB& p = *it;
        bool processed = false;

        // 检查设备类型阻塞
        if (p.blocktype == DEVICEB) {
            // 检查设备申请状态
            if (p.start_block_time == 0) {
                //cout<<"等于0时:"<<p.start_block_time<<endl;
                // 正在申请设备，暂时跳过
                ++it;
                continue;
            } 
            else if (p.start_block_time > 0) {
                // 已获得设备，检查使用时间
                int current_time = time_cnt.load();
                int used_time = current_time - p.start_block_time;
                
                if (used_time >= p.need_block_time) {
                    // 设备使用完成
                    p.state = READY;
                    p.blocktype = NOTBLOCK;
                    p.start_block_time = 0;
                    p.need_block_time = 0;
                    PCB np=p;
                    // 从阻塞队列移除
                    std::cout << "[INFO] Process " << p.pid 
                              << " finished using device and moved to ready queue" << std::endl;
                    it = blockList.erase(it);
                    np.instruction="";
                    // 加入就绪队列
                    ready(np);
                    processed = true;
                }
            } 
            else {
                // 申请失败的情况 (start_block_time < 0)
                
                if (p.apply_time >= MAX_APPLY_TIME) {
                    // 超过最大申请次数，挂起进程
                    p.state = SUSPEND;
                    it = blockList.erase(it);
                    suspend(p);
                    processed = true;
                    
                    std::cout << "[INFO] Process " << p.pid 
                              << " exceeded max apply attempts, suspended" << std::endl;
                } 
                else {
                    // 未超过最大申请次数，重新加入就绪队列
                    if(p.nodevice_time==-1){
                        p.apply_time++;
                        p.nodevice_time = time_cnt.load();//开始阻塞计时
                    }
                    else{
                        int current_time = time_cnt.load();
                        int used_time = current_time - p.nodevice_time;
                        if (used_time >=  SINGE_BLOCK_TIME) {
                            //阻塞计时结束，重新回到就绪队列尝试
                            p.state = READY;
                            p.blocktype = NOTBLOCK;
                            it = blockList.erase(it);
                            ready(p);
                            processed = true;
                            
                            std::cout << "[INFO] Process " << p.pid 
                                << " failed to get device, returning to ready queue" << std::endl;
                            p.nodevice_time = -1;
                        }
                    }
                    
                }
            }
        }

        // 如果没有处理过当前进程，移动到下一个
        if (!processed) {
            ++it;
        }
    }
    blockList_mutex.unlock();
}
void MidStageScheduler()
{
    //根据阻塞类型，将部分手动阻塞的进程换出
    //进程状态---阻塞->挂起
    while(!blockList.empty()){
        blockList_mutex.lock();
        PCB& p = blockList.front();
        blockList.pop_front();
        if(p.blocktype==SYSTEMB){
            if(page_in(p.address,p.pid)){
                p.state = SUSPEND;
                suspend(p);
                cout << "进程 " << p.pid << " 被挂起" << endl;
            }
            else{
                blockList.push_back(p);
            }
        }
        else{
            blockList.push_back(p);
        }
        blockList_mutex.unlock();
    }
}

void shortScheduler() {
    std::lock_guard<std::mutex> lock(ready_list_mutex);

    // 1. 平衡队列 - 如果队列0的进程数比队列1多，移动一个最低优先级的进程到队列1
    /*if (readyList0.size() > readyList1.size()+3) {
        // 找到队列0中优先级最低的进程
        auto min_priority_it = std::min_element(readyList0.begin(), readyList0.end(),
            [](const PCB& a, const PCB& b) {
                return a.priority < b.priority;
            });
        
        if (min_priority_it != readyList0.end()) {
            // 将找到的进程移动到队列1
            PCB process = *min_priority_it;
            readyList0.erase(min_priority_it);
            process.cpu_num = 1; // 更新CPU编号
            readyList1.push_back(process);
            std::cout << "将进程 " << process.pid << " 从CPU0队列移动到CPU1队列以平衡负载" << std::endl;
        }
    }*/

    // 2. 对队列0进行优先级排序（优先级高的在前）
    readyList0.sort([](const PCB& a, const PCB& b) {
        return a.priority > b.priority;
    });

    // 3. 对队列1进行响应比排序
    //updateRRAndSortByRR(readyList1);

    // 打印调度结果
    /*std::cout << "\n===== 调度结果 =====" << std::endl;
    std::cout << "CPU0队列(优先级调度):" << std::endl;
    for (const auto& p : readyList0) {
        std::cout << "进程" << p.pid << " 优先级:" << p.priority << std::endl;
    }

    std::cout << "\nCPU1队列(响应比调度):" << std::endl;
    for (const auto& p : readyList1) {
        std::cout << "进程" << p.pid << " 响应比:" << p.RR << std::endl;
    }*/

    
}



void ProcessStatusManager::updateOverview() {
    ProcessOverviewForUI& overview = current_status.overview;
    
    overview.total_process = PCBList.size();
    overview.running_process = 0;
    overview.blocked_process = blockList.size();
    
    // 统计运行中的进程
    for (const auto& pcb : PCBList) {
        if (pcb.state == RUNNING) {
            overview.running_process++;
        }
    }
}

void ProcessStatusManager::updateProcessTable() {
    current_status.process_table.clear();
    
    for (const auto& pcb : PCBList) {
        ProcessTableItemForUI item;
        
        // 从进程控制块提取信息
        item.name = pcb.position.substr(pcb.position.find_last_of('/') + 1);
        item.pid = pcb.pid;
        item.state = getProcessStateString(pcb.state);
        item.user = "user";  // 可以根据需要设置用户信息
        item.cpu_num = pcb.cpu_num;
        item.memory_size = pcb.task_size;
        
        // 设置描述信息
        if (pcb.state == DEAD) {
            item.description = "正常退出";
        } else if (pcb.blocktype != NOTBLOCK) {
            item.description = "被阻塞: " + std::to_string(pcb.start_block_time);
        } else {
            item.description = "正常运行中";
        }
        
        current_status.process_table.push_back(item);
    }
    current_status.ready_list.clear();
    
    std::lock_guard<std::mutex> lock(ready_list_mutex);
    for (const auto& pcb : readyList0) {
        ProcessReadyListItem item;
        item.name=pcb.position.substr(pcb.position.find_last_of('/') + 1);
        item.pid=pcb.pid;
        item.priority=pcb.priority;
        item.RR=pcb.RR;
        current_status.ready_list.push_back(item);
    }
    ready_list_mutex.unlock();

}

void ProcessStatusManager::updateCPUStatus() {
    std::vector<CPUStatusForUI>& cpu_status = current_status.overview.cpu_status;
    cpu_status.clear();
    
    // 创建 CPU0 状态
    {
        CPUStatusForUI cpu0s;
        cpu0s.cpu_id = 0;
        
        std::lock_guard<std::mutex> lock(ready_list_mutex);
        if (cpu0.running) {
            // CPU正在运行进程
            if (cpu0.running_process != nullptr) {
                cpu0s.current_instruction = cpu0.running_process->instruction;
                cpu0s.remaining_time = cpu0.running_process->current_instruction_time;
                cpu0s.running_pid = cpu0.running_process->pid;
            } else {
                cpu0s.current_instruction = "空闲";
                cpu0s.remaining_time = 0;
                cpu0s.running_pid = -1;
            }
        } else {
            // CPU未运行或正在处理中断
            if (!InterruptQueue.empty()) {
                cpu0s.current_instruction = "处理中断";
            } else {
                cpu0s.current_instruction = "空闲";
            }
            cpu0s.remaining_time = 0;
            cpu0s.running_pid = -1;
        }
        cpu_status.push_back(cpu0s);
    }
    
    
}
std::string ProcessStatusManager::getProcessStateString(int state) {
    switch (state) {
        case CREATING: return "创建中";
        case READY: return "就绪";
        case RUNNING: return "运行中";
        case BLOCK: return "阻塞";
        case SUSPEND: return "挂起";
        case DEAD: return "已终止";
        default: return "未知状态";
    }
}
