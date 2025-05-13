#include "../include/process.h"
#include <list>
#include <string>
#include <iostream>
#include <sstream>
using namespace std;

int pid_num = 1;
int Timer = get_nowSysTime();
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


map<string, struct mutexInfo>fileMutex;

// 占位实现 applyForResource
/*void applyForResource(PCB& p) {
    p.address = alloc_for_process(p.pid, p.task_size);
    if (p.address != FULL) {  //代表内存分配成功，进入就绪队列
        ready(p);           //将进程p由创建状态转入就绪状态
        p.is_apply = true;//成功分配内存
    }
    else {
        suspend(p);
        MidTermScheduler(0,p);
    }
}*/


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
        ready_list_mutex.lock();
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

void Keyboard_delete(PCB& p) {
    for (list<PCB>::iterator i = waitForKeyBoardList.begin(); i != waitForKeyBoardList.end();) {
        if (i->pid == p.pid) {
            waitForKeyBoardList.erase(i++);
        }
        else {
            i++;
        }
    }

};

void Print_delete(PCB& p) {
    for (list<PCB>::iterator i = waitForPrintList.begin(); i != waitForPrintList.end();) {
        if (i->pid == p.pid) {
            waitForPrintList.erase(i++);
        }
        else {
            i++;
        }
    }
};

void FILE_delete(PCB& p) {
    // 在文件互斥锁表中找对应路径
    auto it = fileMutex.find(p.fs);
    if (it == fileMutex.end()) {
        // 没有这个文件的互斥信息，直接返回
        return;
    }

    auto& waitList = it->second.waitForFileList;

    waitList.remove_if([&p](const PCB& pcb) {
        return pcb.pid == p.pid;
        });

    if (waitList.empty()) {
        it->second.isBusy = false;
    }
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

static mutexInfo& getFileMutex(const string& fullpath) {
    auto it = fileMutex.find(fullpath);
    if (it == fileMutex.end()) {
        mutexInfo m;
        m.path = fullpath;
        tie(it, ignore) = fileMutex.emplace(fullpath, move(m));
    }
    return it->second;
}

/*
PCB* getRunningPCB(int cpu_num) {
    for (auto& pcb : PCBList) {
        if (pcb.pid == (cpu_num == 0 ? cpu0.pid : cpu1.pid) && pcb.state == RUNNING) {
            return &pcb;
        }
    }
    return nullptr;
}*/

void waitForFile(string filePath, string fileName) {
    int Timer = get_nowSysTime();
    cout << "filePath" << filePath;
    if (fileMutex[filePath].isBusy == false) {//如果当前的临界区没有进程访问
        if (!fileMutex[filePath].waitForFileList.empty()) {
            cout << "Timer=" << Timer << " 进程pid=" << fileMutex[filePath].waitForFileList.front().pid << "开始I/O文件:" << filePath << endl;
            fileMutex[filePath].isBusy = true; 

            fileMutex[filePath].waitForFileList.front().filewriteStartTime = Timer;
            for (list<PCB>::iterator it = blockList.begin(); it != blockList.end(); ++it) {
                if (it->pid == fileMutex[filePath].waitForFileList.front().pid) {
                    it->filewriteStartTime = Timer;
                }
            }

        }
    }
}
/*
void waitForKeyBoard() {
    //检查当前的wait队列 看是否有进程正在等待或占用键盘
    int Timer = get_nowSysTime();
    if (DeviceControl() != -1) {//如果键盘未被占用
        if (!waitForKeyBoardList.empty()) {
            cout << "Timer=" << Timer << " 进程pid=" << waitForKeyBoardList.front().pid << "开始占用键盘" << endl;
            DeviceControl();
            waitForKeyBoardList.front().keyboardStartTime = Timer;  //将第一个进程开始占用keyboard
            for (list<PCB>::iterator it = blockList.begin(); it != blockList.end(); ++it) {
                if (it->pid == waitForKeyBoardList.front().pid) {
                    it->keyboardStartTime = Timer;
                }
            }
        }
    }
}*/
/*
void waitForPrint() {
    //检查当前的wait队列 看是否有进程正在等待或占用键盘
    int Timer = get_nowSysTime();
    if (DeviceControl() != -1) {
        if (!waitForPrintList.empty()) {
            DeviceControl();
            waitForPrintList.front().printStartTime = Timer;
            for (list<PCB>::iterator it = blockList.begin(); it != blockList.end(); ++it) {
                if (it->pid == waitForPrintList.front().pid) {
                    it->printStartTime = Timer;
                }
            }
        }
    }
}
*/

// 就绪原语
void ready(PCB& p) {
    p.state = READY;

    ready_list_mutex.lock();
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
    blockList_mutex.lock();
    blockList.push_back(p);
    blockList_mutex.unlock();
    // 从就绪队列中移除
    ready_list_mutex.lock();
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
    p.suspend_time = Timer;      // 记录当前时间
    
    suspendList_mutex.lock();
    suspendList.push_back(p);
    suspendList_mutex.unlock();
    cout << "进程 " << p.pid << " 被挂起" << endl;
}

/*
void check_suspended_processes() {
    for (auto it = suspendList.begin(); it != suspendList.end(); ) {
        PCB& p = *it;
        int suspend_duration = Timer - p.suspend_time;  // 计算挂起时长
        if (suspend_duration > MAX_SUSPEND_TIME) {
            cout << "进程 " << p.pid << " 挂起时间过长 (" << suspend_duration
                << " > " << MAX_SUSPEND_TIME << ")，强制终止" << endl;
            stop(p);           // 终止进程
            it = suspendList.erase(it);  // 从挂起队列移除
        }
        else {
            ++it;  // 移动到下一个进程
        }
    }
}*/


// 定义解析结果结构体
struct ProcessInfo {
    int M = -1;      // 内存块数
    int Y = -1;      // 优先级
};

// 解析文件内容
ProcessInfo parseFileContent(const string& content) {
    ProcessInfo info;
    if (content.empty()) return info;

    istringstream iss(content);
    string line;

    // 解析 M
    if (!getline(iss, line)) {
        cerr << "文件格式错误: 缺少M" << endl;
        return info;
    }
    size_t mPos = line.find("M=");
    if (mPos == string::npos) {
        cerr << "文件格式错误: 第一行缺少M=" << endl;
        return info;
    }
    info.M = stoi(line.substr(mPos + 2));

    // 解析 Y
    if (!getline(iss, line)) {
        cerr << "文件格式错误: 缺少Y" << endl;
        return info;
    }
    size_t yPos = line.find("Y=");
    if (yPos == string::npos) {
        cerr << "文件格式错误: 第二行缺少Y=" << endl;
        return info;
    }
    info.Y = stoi(line.substr(yPos + 2));

    return info;
}


// 创建PCB
PCB create(const string& path, const string& filename) {
    PCB p;
    p.pid = pid_num;           // 随机生成PID
    pid_num++;
    p.priority = rand() % 10;            // 设置优先级
    p.state = CREATING;       // 初始状态为创建
    p.cpuState = 0;           // 默认特权状态
    p.blocktype = NOTBLOCK;   // 未阻塞

    std::string content = fs.readFile(path, filename);

    p.cpu_num = -1;           // 未分配CPU
    p.task_size = content.size();          // 设置内存块数
    p.is_apply = false;       // 未分配内存
    p.address = FULL;                // 虚拟地址待分配
    p.next_v = 0;         // 读取位置初始化
    p.position = path + "/" + filename; // 保存完整文件路径

    p.cputime = 0;
    p.cpuStartTime = -1;
    p.keyboardStartTime = -1;
    p.printStartTime = -1;
    p.filewriteStartTime = -1;
    p.createtime = Timer;
    p.RR = 0;

    return p;
}


int read_instruction(string& instruction, m_pid pid) {
    instruction.clear(); // 清空输出字符串

    // 通过 pid 查找对应的 PCB
    PCB* pcb = nullptr;
    for (auto& p : PCBList) {
        if (p.pid == pid) {
            pcb = &p;
            break;
        }
    }
    if (pcb == nullptr) {
        cerr << "错误：找不到 PID 为 " << pid << " 的 PCB" << endl;
        return -1;
    }

    v_address current_address = pcb->address + pcb->next_v; // 从 next_v 开始读取
    atom_data data; // 存储单个字符

    // 如果 next_v == 0，跳过 M 和 Y 两行
    if (pcb->next_v == 0) {
        int newline_count = 0;
        while (newline_count < 2) {
            int result = read_memory(&data, current_address, pid);
            if (result == -1) {
                return -1; // 读取失败，不更新 next_v
            }
            if (data == '?') {
                return 0; // 文件结束
            }
            pcb->next_v++; // 成功读取，更新 next_v
            current_address++;
            if (data == '\n') {
                newline_count++;
            }
        }
    }

    // 读取指令
    while (true) {
        int result = read_memory(&data, current_address, pid);
        if (result == -1) {
            return -1; // 读取失败，不更新 next_v
        }
        if (data == '?') {
            return 0; // 文件结束
        }
        pcb->next_v++; // 成功读取，更新 next_v

        if (data == '\n') {
            break; // 遇到换行符，指令读取完成
        }
        instruction += data;
        current_address++;
    }

    return 1; // 成功读取
}

void CreatePCB()
{
    while(!prePCBList.empty()&&PCBList.size() < MAX_PCB_SIZE&&suspendList.size() < MAX_SPCB_SIZE){
        prePCBList_mutex.lock();
        //创建pcb
        PCB newProcess = create(prePCBList.front().path, prePCBList.front().filename);
        //分配虚拟地址空间
        alloc_for_process(newProcess.pid, newProcess.task_size);
        PCBList.push_back(newProcess);
        suspendList_mutex.lock();
        suspendList.push_back(newProcess);
        suspendList_mutex.unlock();
        prePCBList.pop_front();
        prePCBList_mutex.unlock();
    }
}
void AllocateMemoryForPCB()
{
    while (!suspendList.empty()) {
        suspendList_mutex.lock();
        PCB& p = suspendList.front();
        suspendList.pop_front();
        if(p.state==CREATING){
            if(page_in(p.address,p.pid)){
                std::cout << "进程 " << p.pid << " 分配内存成功" << std::endl;
                p.state = READY;
                ready(p);
            }
            else{
                std::cout << "进程 " << p.pid << " 分配内存失败" << std::endl;
                suspendList.push_back(p);
            }
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
    //小于0表示申请失败，apply_time+1,检查applytime是否超过上限，超过直接挂起进程，不超过则返回就绪队列重新申请
    
    blockList_mutex.lock();
    for (auto it = blockList.begin(); it != blockList.end();) {
        PCB& p = *it;
        bool processed = false;

        // 检查设备类型阻塞
        if (p.blocktype == DEVICEB) {
            // 检查设备申请状态
            if (p.start_block_time == 0) {
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
                    
                    // 从阻塞队列移除
                    it = blockList.erase(it);
                    
                    // 加入就绪队列
                    ready(p);
                    processed = true;
                    
                    std::cout << "[INFO] Process " << p.pid 
                              << " finished using device and moved to ready queue" << std::endl;
                }
            } 
            else {
                // 申请失败的情况 (start_block_time < 0)
                p.apply_time++;
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
                    p.state = READY;
                    p.blocktype = NOTBLOCK;
                    it = blockList.erase(it);
                    ready(p);
                    processed = true;
                    
                    std::cout << "[INFO] Process " << p.pid 
                              << " failed to get device, returning to ready queue" << std::endl;
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
    ready_list_mutex.lock();

    // 1. 平衡队列 - 如果队列0的进程数比队列1多，移动一个最低优先级的进程到队列1
    if (readyList0.size() > readyList1.size()) {
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
    }

    // 2. 对队列0进行优先级排序（优先级高的在前）
    readyList0.sort([](const PCB& a, const PCB& b) {
        return a.priority > b.priority;
    });

    // 3. 对队列1进行响应比排序
    updateRRAndSortByRR(readyList1);

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

    ready_list_mutex.unlock();
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
    
    // 创建 CPU1 状态
    {
        CPUStatusForUI cpu1s;
        cpu1s.cpu_id = 1;
        
        std::lock_guard<std::mutex> lock(ready_list_mutex);
        if (cpu1.running) {
            // CPU正在运行进程
            if (cpu1.running_process != nullptr) {
                cpu1s.current_instruction = cpu1.running_process->instruction;
                cpu1s.remaining_time = cpu1.running_process->current_instruction_time;
                cpu1s.running_pid = cpu1.running_process->pid;
            } else {
                cpu1s.current_instruction = "空闲";
                cpu1s.remaining_time = 0;
                cpu1s.running_pid = -1;
            }
        } else {
            // CPU未运行或正在处理中断
            if (!InterruptQueue.empty()) {
                cpu1s.current_instruction = "处理中断";
            } else {
                cpu1s.current_instruction = "空闲";
            }
            cpu1s.remaining_time = 0;
            cpu1s.running_pid = -1;
        }
        cpu_status.push_back(cpu1s);
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

void ProcessStatusManager::update() {
    if (!need_update) {
        return;
    }
    
    updateOverview();
    updateProcessTable();
    updateCPUStatus();
    
    need_update = false;
}

const ProcessSystemStatusForUI& ProcessStatusManager::getCurrentStatus() const {
    return current_status;
}

// 在需要发送状态时调用此函数
std::string ProcessStatusManager::generateStatusJson() const {
    // 这里需要实现JSON序列化
    // 可以使用rapidjson或其他JSON库
    return ""; // TODO: 实现JSON序列化
}


