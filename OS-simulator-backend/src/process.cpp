#include "process.h"
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

list<PCB> waitForKeyBoardList;//等待键盘队列
list<PCB> waitForPrintList;//等待打印机队列

CPU cpu1;
CPU cpu0;

list<PCB> cpusche0; // CPU0 的调度队列
list<PCB> cpusche1; // CPU1 的调度队列

map<string, struct mutexInfo>fileMutex;

// 占位实现 applyForResource
void applyForResource(PCB& p) {
    p.address = alloc_for_process(p.pid, p.task_size);
    if (p.address != FULL) {  //代表内存分配成功，进入就绪队列
        ready(p);           //将进程p由创建状态转入就绪状态
        p.is_apply = true;//成功分配内存
    }
    else {
        suspend(p);
        MidTermScheduler(0,p);
    }
}

int applyForSuspend(PCB& p) {
    // 尝试为挂起进程申请内存
    p.address = alloc_for_process(p.pid, p.task_size);

    if (p.address != FULL) {  // 如果内存分配成功
        p.is_apply = true;    // 标记为内存分配成功
        ready(p);             // 将进程从挂起状态转移到就绪状态
        return 1;             // 返回1表示申请成功
    }
    else {
        // 如果内存分配失败，则继续挂起进程
        return 0;             // 返回0表示申请失败
    }
}

v_address alloc_for_process(m_pid pid, int size) {
    return 0;
}


vector<char> virtual_memory(1024 * 10);

// read_memory 函数
int read_memory(atom_data* data, v_address address, m_pid pid) {
    // 每个进程的虚拟地址范围：(pid - 1) * 1024 到 pid * 1024 - 1
    v_address base = (pid - 1) * 1024;
    v_address limit = pid * 1024;

    // 检查地址是否在有效范围内
    if (address < base || address >= limit || address >= virtual_memory.size()) {
        return -1; // 地址越界或无效
    }

    // 读取数据
    *data = virtual_memory[address];
    return 0; // 成功
}

// 文件系统提供的函数
string readFile(string path, string filename) {
    return "M=5\nY=3\nstart\nprocess data\nend";
}

void free_process_memory(int pid) {

}

int createFile(string path, string filename, int fileType, int size) {

}
int deleteFile(string path, string filename) {

}
int DeviceControl() {
    return 0;
}
int writeFile(string path, string filename, string data) {
    return 0;
}
//以上为暂用

void removePCBFromQueue(PCB* current_pcb) {
    // 检查进程是否为空
    if (current_pcb == nullptr) {
        std::cout << "当前进程为空，无法操作" << std::endl;
        return;
    }

    // 根据状态执行不同的操作
    switch (current_pcb->status) {
    case READY:
    {
        // 查找并从对应队列中移除
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
        return a.prority > b.prority;
        };

    readyList0.sort(compareByPriorityDesc);
    readyList1.sort(compareByPriorityDesc);
}
void updateRRAndSortByRR(list<PCB>& readyList) {
    int now = get_nowSysTime();  // 获取当前系统时间

    // 更新每个 PCB 的响应比
    for (auto& pcb : readyList) {
        int serviceTime = pcb.cputime; // 如果 cputime 为 0，响应比设为 1.0
        if (pcb.cputime == 0) {
            pcb.RR = 1.0;
        }
        else {
            pcb.RR = static_cast<double>(now - pcb.createtime) / serviceTime;
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

PCB* getRunningPCB(int cpu_num) {
    for (auto& pcb : PCBList) {
        if (pcb.pid == (cpu_num == 0 ? cpu0.pid : cpu1.pid) && pcb.state == RUNNING) {
            return &pcb;
        }
    }
    return nullptr;
}

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

void waitForKeyBoard() {
    //检查当前的wait队列 看是否有进程正在等待或占用键盘
    int Timer = get_nowSysTime()
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
}

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

void create_file(int time, const string& directory, const string& filename, PCB& p) {
    if (createFile(directory, filename, 0, 1) != -1) {
        p.block_time = Timer;
        p.blocktype = OTHER;
        block(p);
    }
    else {
        cout << "进程" << p.pid << "创建文件失败";
    }
}
void delete_file(int time, const string& directory, const string& filename, PCB& p) {
    if (deleteFile(directory, filename) != -1) {
        p.block_time = Timer;
        p.blocktype = OTHER;
        block(p);
    }
    else {
        cout << "进程" << p.pid << "删除文件失败";
    }
}

void input_from_device(int time, const string& device_type, PCB& p) {
    p.block_time = Timer;
    p.blocktype = KEYBOARD;
    block(p);
    waitForKeyBoardList.push_back(p);
    waitForKeyBoard();
}
void output_to_device(int time, const string& device_type, PCB& p) {
    p.block_time = Timer;
    p.blocktype = PRINT;
    block(p);
    waitForPrintList.push_back(p);
    waitForPrint();
}
void read_file(int time, const string& directory, const string& filename, int line, PCB& p) {

    string fullpath = directory + "/" + filename;


    mutexInfo& m = getFileMutex(fullpath);

    m.waitForFileList.push_back(p);
    p.fsState = "r";
    p.fs = fullpath;
    p.block_time = Timer;
    p.blocktype = FILEB;
    block(p);
    waitForFile(directory, filename);
}
void read_file(int time, const string& directory, const string& filename, PCB& p) {
    string fullpath = directory + "/" + filename;


    mutexInfo& m = getFileMutex(fullpath);

    m.waitForFileList.push_back(p);

    p.block_time = Timer;
    p.blocktype = FILEB;
    block(p);
    waitForFile(directory, filename);
}
void write_file(int time, const string& directory, const string& filename, const string& content, PCB& p) {
    string fullpath = directory + "/" + filename;


    mutexInfo& m = getFileMutex(fullpath);

    m.waitForFileList.push_back(p);

    p.block_time = Timer;
    p.blocktype = FILEB;
    block(p);
    p.content = content;
    waitForFile(directory, filename);
}
void block_process(int time, int pid, PCB& p) {
    p.block_time = Timer;
    p.blocktype = SYSTEM;
    block(p);
}
void wake_process(int time, int pid, PCB& p) {
    p.blocktype = NOTBLOCK;
    ready(p);
}


// 就绪原语
void ready(PCB& p) {
    p.state = READY;
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
}

// 阻塞原语
void block(PCB& p) {

    p.state = BLOCK;
    blockList.push_back(p);
    cout << "进程 " << p.pid << " 被阻塞" << endl;
}

// 结束原语
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
    suspendList.push_back(p);
    cout << "进程 " << p.pid << " 被挂起" << endl;
}

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
}


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
PCB create(const string& path, const string& filename, int M, int Y) {
    PCB p;
    p.pid = pid_num;           // 随机生成PID
    pid_num++;
    p.prority = Y;            // 设置优先级
    p.state = CREATING;       // 初始状态为创建
    p.cpuState = 0;           // 默认特权状态
    p.blocktype = NOTBLOCK;   // 未阻塞

    p.cpu_num = -1;           // 未分配CPU
    p.task_size = M;          // 设置内存块数
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
// 长期调度程序
void LongTermScheduler(string path, string filename) {
    string content = readFile(path, filename);
    if (content.empty()) {
        cerr << "文件读取失败，调度终止" << endl;
        return;
    }

    ProcessInfo info = parseFileContent(content);
    if (info.M == -1 || info.Y == -1) {
        cerr << "解析失败，调度终止" << endl;
        return;
    }

    PCB p = create(path, filename, info.M, info.Y);
    applyForResource(p);  // 在创建 PCB 后申请资源
    PCBList.push_back(p);
}

void `parse_and_execute(const string& instruction, PCB& p) {
    // 将指令分割为 tokens
    istringstream iss(instruction);
    vector<string> tokens;
    string token;
    while (iss >> token) {
        tokens.push_back(token);
    }

    // 检查是否为空指令
    if (tokens.empty()) {
        cout << "空指令" << endl;
        return;
    }

    // 提取命令
    string command = tokens[0];

    // 根据命令类型解析和执行
    if (command == "CREATEFILE" || command == "DELETEFILE") {
        int time = 1; // 默认时间为 1
        size_t idx = 1;
        // 检查时间是否提供
        if (tokens.size() >= 2 && isdigit(tokens[1][0])) {
            time = stoi(tokens[1]);
            idx = 2;
        }
        // 验证参数数量
        if (tokens.size() < idx + 2) {
            cout << command << " 参数不足" << endl;
            return;
        }
        string directory = tokens[idx];
        string filename = tokens[idx + 1];
        if (command == "CREATEFILE") {
            p.block_time = get_nowSysTime();
            p.blocktype = OTHER;
            block(p);
        }
        else {
            p.block_time = get_nowSysTime();
            p.blocktype = OTHER;
            block(p);
        }
    }
    else if (command == "CALCULATE") {
        if (tokens.size() < 2) {
            cout << "CALCULATE 参数不足" << endl;
            return;
        }
        int time = stoi(tokens[1]);
        ready(p);
    }
    else if (command == "INPUT") {
        if (tokens.size() < 3) {
            cout << "INPUT 参数不足" << endl;
            return;
        }
        int time = stoi(tokens[1]);
        string device_type = tokens[2];
        p.block_time = get_nowSysTime();
        p.blocktype = KEYBOARD;
        block(p);
        waitForKeyBoardList.push_back(p);
        waitForKeyBoard();
    }
    else if (command == "OUTPUT") {
        if (tokens.size() < 3) {
            cout << "OUTPUT 参数不足" << endl;
            return;
        }
        int time = stoi(tokens[1]);
        string device_type = tokens[2];
        p.block_time = get_nowSysTime();
        p.blocktype = PRINT;
        block(p);
        waitForPrintList.push_back(p);
        waitForPrint();
    }
    else if (command == "READFILE") {
        if (tokens.size() < 4) {
            cout << "READFILE 参数不足" << endl;
            return;
        }
        int time = stoi(tokens[1]);
        string directory = tokens[2];
        string filename = tokens[3];
        if (tokens.size() == 5) {
            int line = stoi(tokens[4]);
            read_file(time, directory, filename, line, p);
        }
        else {
            read_file(time, directory, filename, p);
        }
    }
    else if (command == "WRITEFILE") {
        if (tokens.size() < 4) {
            cout << "WRITEFILE 参数不足" << endl;
            return;
        }
        int time = stoi(tokens[1]);
        string directory = tokens[2];
        string filename = tokens[3];
        // 将剩余部分作为内容
        string content;
        for (size_t i = 4; i < tokens.size(); ++i) {
            if (i > 4) content += " ";
            content += tokens[i];
        }
        write_file(time, directory, filename, content, p);
    }
    else if (command == "BLOCK") {
        if (tokens.size() < 3) {
            cout << "BLOCK 参数不足" << endl;
            return;
        }
        int time = stoi(tokens[1]);
        int pid = stoi(tokens[2]);
        
    }
    else if (command == "WAKE") {
        if (tokens.size() < 3) {
            cout << "WAKE 参数不足" << endl;
            return;
        }
        int time = stoi(tokens[1]);
        int pid = stoi(tokens[2]);

        if (p.blocktype != SYSTEM) return;

        wake_process(time, pid, p);
    }
    else {
        cout << "未知命令: " << command << endl;
    }
}
void execute() {
    // 遍历两个 CPU
    for (int cpu = 0; cpu < 2; ++cpu) {
        list<PCB>& readyList = (cpu == 0) ? readyList0 : readyList1;

        // 检查就绪队列是否非空
        if (!readyList.empty()) {
            // 遍历就绪队列中的每个 PCB
            for (auto& p : readyList) {
                // 检查 program 是否为空
                readyList.pop_front(); // 从队列中移除
                if (!p.program.empty()) {


                    // 使用 program 中的指令进行解析和执行
                    parse_and_execute(p.program, p);
                }
            }
        }
    }
}

void updateTaskState() {
    int Timer = get_nowSysTime();
    if (!waitForKeyBoardList.empty()) {
        PCB& p = waitForKeyBoardList.front();
        // 1. 解析 "INPUT <time> <device>"
        string cmd = p.program;
        stringstream ss(cmd);
        string op, devType, tstr;
        ss >> op >> tstr >> devType;          // op == "INPUT"

        // 2. 仅处理键盘设备
        if (devType == "KEYBOARD") {
            long serviceTime = stol(tstr);    // 转成数值
            long elapsed = Timer - p.keyboardStartTime;

            // 3. 日志输出
            cout << "pid=" << p.pid
                << " waited=" << elapsed
                << " needed=" << serviceTime
                << endl;

            // 4. 时间到，触发中断
            if (elapsed >= serviceTime-1) {
                p.blocktype = NOTBLOCK;
                p.program.clear();
                ready(p);
                for (list<PCB>::iterator i = blockList.begin(); i != blockList.end();) {
                    if (i->pid == p.pid) {
                        blockList.erase(i++);
                    }
                    else {
                        i++;
                    }
                }
                waitForKeyBoardList.pop_front();
                for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
                    if (it->pid == p.pid) {
                        it->clear();
                    }
                }
                DeviceControl();//释放设备
                waitForKeyBoard();
            }
        }
    }

    if (!waitForPrintList.empty()) {
        PCB& p = waitForPrintList.front();
        // 1. 解析 "OUTPUT <time> <device>"
        string cmd = p.program;
        stringstream ss(cmd);
        string op, devType, tstr;
        ss >> op >> tstr >> devType;          // op == "OUTPUT"

        // 2. 仅处理打印设备
        if (devType == "PRINTER") {
            long serviceTime = stol(tstr);    // 转成数值
            long elapsed = Timer - p.printStartTime;

            // 3. 日志输出
            cout << "pid=" << p.pid
                << " waited=" << elapsed
                << " needed=" << serviceTime
                << endl;

            // 4. 时间到，触发中断
            if (elapsed >= serviceTime-1) {
                p.blocktype = NOTBLOCK;
                p.program.clear();
                ready(p);
                for (list<PCB>::iterator i = blockList.begin(); i != blockList.end();) {
                    if (i->pid == p.pid) {
                        blockList.erase(i++);
                    }
                    else {
                        i++;
                    }
                }
                waitForPrintList.pop_front();
                for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
                    if (it->pid == p.pid) {
                        it->clear();
                    }
                }
                DeviceControl();
                waitForPrint();
            }
        }
    }



    if (!suspendList.empty()) {
        for (auto it = suspendList.begin(); it != suspendList.end(); /*++it 在循环末尾*/) {
            PCB& p = *it;
            long elapsed = Timer - p.suspend_time;

            // 日志输出
            cout << "pid=" << p.pid
                << " suspended=" << elapsed
                << " threshold=" << MAX_SUSPEND_TIME
                << endl;

            if (elapsed >= MAX_SUSPEND_TIME) {
                // 触发强制停止
                stop(p);

                // 从 suspendList 中移除
                it = suspendList.erase(it);
                continue;
            }
            ++it;
        }


    }
    if (!blockList.empty()) {
        for (auto it = blockList.begin(); it != blockList.end(); /*++it 在下面*/) {
            PCB& p = *it;
            long elapsed = Timer - p.block_time;

            // 1. 超时强制停止
            if (elapsed >= MAX_BLOCK_TIME) {
                cout << "pid=" << p.pid
                    << " blockedTooLong=" << elapsed
                    << " > MAX_BLOCK_TIME=" << MAX_BLOCK_TIME
                    << endl;
                stop(p);
                it = blockList.erase(it);
                continue;
            }

            // 2. SYSTEM / OTHER 类型按指令时间解阻
            if (p.blocktype == SYSTEM || p.blocktype == OTHER) {
                // 解析指令
                string op, tstr, arg1, arg2;
                stringstream ss(p.program);
                ss >> op >> tstr;  // 读取操作符和时间

                // 默认时间为1
                long serviceTime = 1;
                if (!tstr.empty()) {
                    try {
                        serviceTime = stol(tstr);  // 将时间参数转换为数字
                    }
                    catch (const std::invalid_argument& e) {
                        cout << "Invalid time format for PID " << p.pid << endl;
                    }
                }

                // 如果存在其他参数，继续读取目录和文件名
                ss >> arg1 >> arg2;

                cout << "pid=" << p.pid
                    << " op=" << op
                    << " elapsed=" << elapsed
                    << " need=" << serviceTime
                    << endl;

                if (elapsed >= serviceTime-1) {
                    // 解除阻塞：置就绪，移入 readyQueue，清空当前指令
                    ready(p);
                    p.program.clear();
                    p.blocktype = NOTBLOCK;
                    it = blockList.erase(it);
                    continue;
                }
            }

            ++it;
        }
    }

    map<string, struct mutexInfo>::iterator it;
    for (auto it = fileMutex.begin(); it != fileMutex.end(); ++it) {
        if (!it->second.isBusy) continue;

        PCB& writeP = it->second.waitForFileList.front();
        const string& instr = writeP.program;  // e.g. "READFILE 5 /dir file.txt 10"


        istringstream ss(instr);
        string cmd, timeStr, directory, filename;
        if (!(ss >> cmd >> timeStr >> directory >> filename)) {
            cerr << "无法解析指令前四项: " << instr << endl;
            continue;
        }


        int instrTime = stoi(timeStr);

        if (cmd == "READFILE") {

            int line = -1;

            if (Timer - writeP.filewriteStartTime >= instrTime) {
                
                for (auto it = blockList.begin(); it != blockList.end(); it++) {
                    PCB& p = *it;
                    if (p.pid == writeP.pid) {
                        ready(p);
                        p.program.clear();
                        it = blockList.erase(it);
                        p.blocktype = NOTBLOCK;
                        break;
                    }

                }
            }
        }
        else if (cmd == "WRITEFILE") {

            string content;

            getline(ss, content);

            if (!content.empty() && content[0] == ' ')
                content.erase(0, 1);

            // 计算是否完成
            if (Timer - writeP.filewriteStartTime >= instrTime) {
                
                for (auto it = blockList.begin(); it != blockList.end(); it++) {
                    PCB& p = *it;
                    if (p.pid == writeP.pid) {
                        ready(p);
                        p.program.clear();
                        it = blockList.erase(it);
                        p.blocktype = NOTBLOCK;
                        break;
                    }

                }
            }
        }
        else {
            cerr << "未知指令类型: " << cmd << endl;
        }
    }

    for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
        if (it->state == RUNNING) {
            it->cputime = Timer - it->cpuStartTime;
            
        }
    }
    
    MidTermScheduler(1,null);

}

void MidTermScheduler(int inOrOut, PCB& p) {
    int Timer = get_nowSysTime();
    if (inOrOut == 0) {//OUT
        int size = 0;
        for (int i = 0; i < blockList.size(); ++i) {
            pair<int, int> pcbPair = { INT_MAX, -1 };
            for (list<PCB>::iterator it = blockList.begin(); it != blockList.end(); ++it) {
                if (it->prority < pcbPair.first && it->is_apply) {//优先调出优先级低的进程
                    pcbPair.first = it->prority;
                    pcbPair.second = it->pid;
                }
            }
            if(p.prority < pcbPair.first){
                return;
            }

            if (pcbPair.second != -1) {
                for (list<PCB>::iterator it = blockList.begin(); it != blockList.end(); ++it) {
                    if (it->pid == pcbPair.second) {
                        size += it->task_size;
                        it->is_apply = false;
                        PCB& p = *it;

                        if (Under_Keyboard(p)) {
                            Keyboard_delete(p);
                            cout << "pid=" << p.pid << " wTime " << p.keyboardStartTime << endl;

                            if (p.keyboardStartTime != -1) {
                                // 按 “操作码 时间 设备” 拆分
                                istringstream iss(p.program);
                                string op, dev;
                                int origTime;
                                iss >> op >> origTime >> dev;

                                if (op == "INPUT") {
                                    // 计算已执行时间和剩余时间
                                    int elapsed = Timer - p.keyboardStartTime;
                                    int remTime = origTime - elapsed;
                                    cout << "keyTime=" << remTime << endl;

                                    if (remTime > 0) {
                                        // 还有剩余，更新 p.program
                                        p.program = op + " " + to_string(remTime) + " " + dev;
                                    }
                                    else {
                                        // 时间耗尽，清空指令或切换下一步
                                        p.program.clear();
                                    }
                                }
                            }
                        }
                        if (Under_Print(p)) {
                            Print_delete(p);
                            p.blocktype = PRINT;
                            cout << "pid=" << p.pid << " wTime " << p.printStartTime << endl;

                            if (p.printStartTime != -1) {
                                // 按 “操作码 时间 设备” 拆分
                                istringstream iss(p.program);
                                string op, dev;
                                int origTime;
                                iss >> op >> origTime >> dev;

                                if (op == "OUTPUT") {
                                    // 计算已执行时间和剩余时间
                                    int elapsed = Timer - p.printStartTime;
                                    int remTime = origTime - elapsed;
                                    cout << "printTime=" << remTime << endl;

                                    if (remTime > 0) {
                                        // 还有剩余，更新 p.program
                                        p.program = op + " " + to_string(remTime) + " " + dev;
                                    }
                                    else {
                                        // 打印完成，清空或切到下一条
                                        p.program.clear();
                                    }
                                }
                            }
                        }
                        if (p.blocktype == FILEB) {
                            FILE_delete(p);

                        }
                        suspend(p);
                        for (list<PCB>::iterator i = blockList.begin(); i != blockList.end();) {
                            if (i->pid == p.pid) {
                                blockList.erase(i++);
                            }
                            else {
                                i++;
                            }
                        }


                        free_process_memory(it->pid);
                        return;
                        
                    }
                }
            }
        }
    }
    else if (inOrOut == 1) {//IN
        pair<int, int> pcbPair = { INT_MIN, -1 };
        for (list<PCB>::iterator it = suspendList.begin(); it != suspendList.end(); ++it) {
            if (it->prority > pcbPair.first && it->task_size < blocks) {
                pcbPair.first = it->prority;
                pcbPair.second = it->pid;
            }
        }
        if (pcbPair.second != -1) {
            for (list<PCB>::iterator it = suspendList.begin(); it != suspendList.end(); ++it) {
                if (it->pid == pcbPair.second) {
                    PCB& p = *it;
                    
                    applyForSuspend(p)
                    return;
                }
            }
        }
    }
}


void CPUScheduler(PCB& p, int cpu) {
    p.cpu_num = cpu;         // 分配指定的 CPU
    p.state = RUNNING;       // 设置进程状态为运行
    p.cpuStartTime = Timer;  // 记录 CPU 开始时间
    if (cpu == 0) {
        cpu0.isbusy = true;
        cpu0.pid = p.pid;
    }
    else {
        cpu1.isbusy = true;
        cpu1.pid = p.pid;
    }
}

