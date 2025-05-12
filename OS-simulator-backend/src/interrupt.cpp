#include "../include/interrupt.h"

//新的中断处理函数参数 1.int32 2.int32 3.string 4.int* 5.int32

InterruptVector InterruptVectorTable[InterruptVectorTableSize]; //中断向量表
std::mutex iq; //中断队列锁
std::atomic<uint16_t> valid; //valid的每个二进制位代表对应的中断类型是否有效
std::atomic<bool> timerInterruptValid; //时钟中断是否有效标志,时钟在单独的线程运行，这里仅控制时钟是否产生中断，不能控制时钟线程是否存在
std::atomic<bool> stopTimerFlag; //直接停止时钟中断线程
std::atomic<bool> handleFlag;//中断正在处理标志
std::priority_queue<Interrupt, std::vector<Interrupt>, InterruptComparator> InterruptQueue; //中断等待队列
std::atomic<long long> time_cnt;
std::queue<Interrupt> readyInterruptQueue;//存储在中断执行过程中产生的中断
time_t startSysTime;
time_t nowSysTime;
std::thread th[2];
std::atomic<int> interrupt_handling_cpus{0};  // 定义正在处理中断的CPU数量
int scheduel;

void Interrupt_Init(){ //中断初始化
    //初始化中断有效位
    valid=0x0000|1<<static_cast<int>(InterruptType::TIMER)
    |1<<static_cast<int>(InterruptType::DEVICE)
    |1<<static_cast<int>(InterruptType::SOFTWARE)
    |1<<static_cast<int>(InterruptType::SNAPSHOT)
    |1<<static_cast<int>(InterruptType::NON_MASKABLE)
    |1<<static_cast<int>(InterruptType::PAGEFAULT)
    |1<<static_cast<int>(InterruptType::TEST)
    |1<<static_cast<int>(InterruptType::MERROR);

    InterruptVector Timer(noHandle, static_cast<int>(InterruptType::TIMER));
    InterruptVectorTable[static_cast<int>(InterruptType::TIMER)]=Timer;
    InterruptVector Device(noHandle, static_cast<int>(InterruptType::DEVICE));
    InterruptVectorTable[static_cast<int>(InterruptType::DEVICE)]=Device;
    InterruptVector Software(noHandle, static_cast<int>(InterruptType::SOFTWARE));
    InterruptVectorTable[static_cast<int>(InterruptType::SOFTWARE)]=Software;
    InterruptVector Snapshot(snapshotSend, static_cast<int>(InterruptType::SNAPSHOT));
    InterruptVectorTable[static_cast<int>(InterruptType::SNAPSHOT)]=Snapshot;
    InterruptVector Non_maskable(noHandle, static_cast<int>(InterruptType::NON_MASKABLE));
    InterruptVectorTable[static_cast<int>(InterruptType::NON_MASKABLE)]=Non_maskable;
    InterruptVector PageFault(Pagefault, static_cast<int>(InterruptType::PAGEFAULT));
    InterruptVectorTable[static_cast<int>(InterruptType::PAGEFAULT)]=PageFault;
    InterruptVector Test(noHandle, static_cast<int>(InterruptType::TEST));
    InterruptVectorTable[static_cast<int>(InterruptType::TEST)]=Test;
    InterruptVector Error(errorHandle, static_cast<int>(InterruptType::MERROR));
    InterruptVectorTable[static_cast<int>(InterruptType::MERROR)]=Error;
    //清空中断处理队列
    while(!InterruptQueue.empty())
        InterruptQueue.pop();
    while(!readyInterruptQueue.empty())
        readyInterruptQueue.pop();
    //设置标志位
    handleFlag.store(0);
    stopTimerFlag.store(0);
    timerInterruptValid.store(0);
    time_cnt.store(0);
    th[0]=std::thread(TimeThread,Normal_Timer_Interval);
    th[0].detach();
    //待添加一个接受前端指令的进程
}

void noHandle(int v1,int v2,std::string v3,int* v4, int v5){};
void errorHandle(int v1,int v2,std::string v3,int* v4, int v5){
    exit(99);
}
//中断产生
void raiseInterrupt(InterruptType t, int v1, int v2, std::string v3,int* v4,int v5){
    incrementInterruptCount(t);
    Interrupt itp(t, v1, v2, v3, v4, v5);
    if(handleFlag.load()){
        readyInterruptQueue.push(itp);
    }else{
        iq.lock();
        InterruptQueue.push(itp);
        iq.unlock();
    } 
}
//中断处理
void handleInterrupt(){
    // 增加处理中断的CPU计数
    interrupt_handling_cpus++;
    // 等待另一个CPU也进入中断处理
    while (interrupt_handling_cpus < 2) {
        std::this_thread::yield();
    }
    handleFlag.store(1);
    while(!InterruptQueue.empty()){
        iq.lock();
        if (InterruptQueue.empty()) {
            return;
        }
        Interrupt tmp=InterruptQueue.top();
        InterruptQueue.pop();
        iq.unlock();
        InterruptVectorTable[static_cast<int>(tmp.type)].handler(tmp.value1,tmp.value2,tmp.value3,tmp.value4,tmp.value5);
    }
    iq.lock();
    while(!readyInterruptQueue.empty()){
        Interrupt tmp=readyInterruptQueue.front();
        InterruptQueue.push(tmp);
        readyInterruptQueue.pop();
    }
    iq.unlock();
    // 减少处理中断的CPU计数
    interrupt_handling_cpus--;
    handleFlag.store(0);
}
//工具函数
bool InterruptTool::isValid(InterruptType t) {
    // 只能查询可屏蔽中断的状态
    if (static_cast<int>(t) >= static_cast<int>(InterruptType::NON_MASKABLE)) {
        return true;  // 不可屏蔽中断始终有效
    }
    return valid.load() & (1 << static_cast<int>(t));
}

bool InterruptTool::setValid(InterruptType t) {
    // 只能设置可屏蔽中断
    if (static_cast<int>(t) >= static_cast<int>(InterruptType::NON_MASKABLE)) {
        return false;  // 不允许修改不可屏蔽中断
    }
    valid.store(valid.load() | (1 << static_cast<int>(t)));
    return true;
}

bool InterruptTool::unsetValid(InterruptType t) {
    // 只能设置可屏蔽中断
    if (static_cast<int>(t) >= static_cast<int>(InterruptType::NON_MASKABLE)) {
        return false;  // 不允许修改不可屏蔽中断
    }
    valid.store(valid.load() & ~(1 << static_cast<int>(t)));
    return true;
}

bool InterruptTool::allValid() {
    // 只设置可屏蔽中断的有效位
    uint16_t maskable_interrupts = (1 << static_cast<int>(InterruptType::NON_MASKABLE)) - 1;
    valid.store((valid.load() & ~maskable_interrupts) | maskable_interrupts);
    return true;
}

bool InterruptTool::setPriority(InterruptType t, int mpriority) {
    // 只能设置可屏蔽中断的优先级
    if (static_cast<int>(t) >= static_cast<int>(InterruptType::NON_MASKABLE)) {
        return false;  // 不允许修改不可屏蔽中断的优先级
    }
    InterruptVectorTable[static_cast<int>(t)].priority = mpriority;
    return true;
}
bool InterruptTool::enableTimerInterrupt(){
    timerInterruptValid.store(1);
    return true;
}
bool InterruptTool::disableTimerInterrupt(){
    timerInterruptValid.store(0);
    return true;
}
bool InterruptTool::stopTimer(){
    stopTimerFlag.store(1);
    return true;
}


 
void delay(int timeout_ms)
{
    auto start = std::chrono::system_clock::now();
    while (true)
    {
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
    if (duration > timeout_ms)
      break;
    }
}
void TimeThread(int interval = Normal_Timer_Interval) {
    time(&startSysTime); // 获取系统启动时间
    nowSysTime = startSysTime; // 初始化当前系统时间
    while (!stopTimerFlag.load()) {
        //std::cout << "Timer thread running..." << std::endl; // 调试输出
        if (timerInterruptValid.load()) {
            raiseInterrupt(InterruptType::TIMER, 0, 0, "", nullptr, 0);
        }
        time_cnt.fetch_add(1);
        if((valid.load()|1<<static_cast<int>(InterruptType::SNAPSHOT))&&time_cnt.load()%10==0){
            raiseInterrupt(InterruptType::SNAPSHOT, 0, 0, "", nullptr, 0);
        }
        nowSysTime =startSysTime+(time_cnt.load()*interval)/ 1000; // interval 是毫秒，转换为秒
        //std::cout << timeToChar(nowSysTime) << std::endl;
        // 延迟 interval 毫秒
        delay(interval);
    }
    std::cout << "Timer thread stopped." << std::endl; 
}
char* timeToChar(time_t time){
    return ctime(&time);
}
struct tm* timeToStruct(time_t time){
    return localtime(&time);
}

time_t get_startSysTime(){
    return startSysTime;
}

time_t get_nowSysTime(){
    return nowSysTime;
}

//以下为主程序函数


//运行一条指令
bool RUN(std::string cmd, PCB* current_pcb){
    std::vector<std::string> scmd;
    CmdSplit(cmd,scmd);
    if(scmd.size()<2){
        raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
        return false;
    }
    else{
        std::string cmdType=scmd[0];
        uint32_t ntime=stoi(scmd[1]);
        if(cmdType=="CREATEFILE"){
            if(scmd.size()<4){
                raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
                return true;
            }
            else{
                std::string cata=scmd[2];
                std::string filename=scmd[3];
                fs.createFile(cata,filename,0,128);
                return true;
            }

        }else if(cmdType=="DELETEFILE"){
            if(scmd.size()<4){
                raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
                return true;
            }
            else{
                std::string cata=scmd[2];
                std::string filename=scmd[3];
                fs.deleteFile(cata, filename);
                return true;
            }

        }else if(cmdType=="CALCULATE"){
            ;
            return true;
        }else if(cmdType=="INPUT"){
            if(scmd.size()<3){
                raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
                return false;
            }else{
               //产生设备中断，后续补充
                int devicetype=stoi(scmd[2]);
                int needtime=stoi(scmd[3]);
                current_pcb->deviceStartTime = time_cnt;
                current_pcb->deviceTime = needtime;
                raiseInterrupt(InterruptType::DEVICE,current_pcb->pid,devicetype,"",&current_pcb->block_time,needtime);
            }

        }else if(cmdType=="OUTPUT"){
            if(scmd.size()<3){
                raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
                return false;
            }else{
                //产生设备中断，后续补充
                int devicetype=stoi(scmd[2]);
                int needtime=stoi(scmd[3]);
                current_pcb->deviceStartTime = time_cnt;
                current_pcb->deviceTime = needtime;
                raiseInterrupt(InterruptType::DEVICE,current_pcb->pid,devicetype,"",&current_pcb->block_time,needtime);
            }

        }else if(cmdType=="READFILE"){
            if(scmd.size()<4){
                raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
                return true;
            }else{
                std::string cata=scmd[2];
                std::string filename=scmd[3];
                fs.readFile(cata, filename);
                return true;
            }

        }else if(cmdType=="WRITEFILE"){
            if(scmd.size()<4){
                raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
                return true;
            }
            else{
                std::string cata=scmd[2];
                std::string filename=scmd[3];
                std::string content=scmd[4];
                fs.writeFile(cata, filename, content);
                return true;
            }

        }else if(cmdType=="BLOCK"){
            if(scmd.size()<3){
                raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
                return true;
            }else{
                int tpid=stoi(scmd[2]);

                PCB found;
                bool isFound = false; // 标记是否找到进程
                std::lock_guard<std::mutex> lock(ready_list_mutex);
                readyList0.lock;
                readyList1.lock;
                blockList.lock;
                // 查找并移除进程
                for (auto it = readyList0.begin(); it != readyList0.end(); ++it) {
                    if (it->pid == tpid) {
                        found = *it;
                        readyList0.erase(it);  // 从 readyList0 中移除
                        isFound = true;
                        break;
                    }
                }

                if (!isFound) {
                    for (auto it = readyList1.begin(); it != readyList1.end(); ++it) {
                        if (it->pid == tpid) {
                            found = *it;
                            readyList1.erase(it);  // 从 readyList1 中移除
                            isFound = true;
                            break;
                        }
                    }
                }

                if (isFound) {
                    std::cout << "Process with PID " << tpid << " has been blocked." << std::endl;
                    found.blocktype = SYSTEM;
                    found.block_time = get_nowSysTime();
                    block(found);
                }
                else {
                    std::cout << "Process does not exist or process can't be blocked." << std::endl;
                    std::lock_guard<std::mutex> unlock(ready_list_mutex);
                    readyList0.unlock;
                    readyList1.unlock;
                    blockList.unlock;
                    return true;
                }
                std::lock_guard<std::mutex> unlock(ready_list_mutex);
                readyList0.unlock;
                readyList1.unlock;
                blockList.unlock;
                return true;
            }

        }else if(cmdType=="WAKE"){
            if(scmd.size()<3){
                raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
                return true;
            }else{
                int tpid=stoi(scmd[2]);  

                PCB found;
                bool isFound = false; // 标记是否找到进程
                std::lock_guard<std::mutex> lock(ready_list_mutex);
                readyList0.lock;
                readyList1.lock;
                blockList.lock;
                for (auto it = blockList.begin(); it != blockList.end(); ++it) {
                    if (it->pid == tpid) {
                        found = *it;
                        if (found.blocktype == SYSTEM)
                            blockList.erase(it);
                        else
                            break;
                        isFound = true;
                        break;
                    }
                }

                if (isFound) {
                    std::cout << "Process with PID " << tpid << " has been waken." << std::endl;
                    found.blocktype = NOTBLOCK;
                    found.block_time = 0;
                    ready(found);
                }
                else {
                    std::cout << "Process does not exist or process can't be waken." << std::endl;
                    std::lock_guard<std::mutex> unlock(ready_list_mutex);
                    readyList0.unlock;
                    readyList1.unlock;
                    blockList.unlock;
                    return true;
                }
                std::lock_guard<std::mutex> unlock(ready_list_mutex);
                readyList0.unlock;
                readyList1.unlock;
                blockList.unlock;
                return true;
            }
        }else{
            raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
            return true;
        }
    }
    return true;
}
//分割指令
void CmdSplit(const std::string& cmd, std::vector<std::string>& scmd) {
    std::istringstream iss(cmd);
    std::string tmp;
    int word_count = 0;
    
    // 读取前3个词
    while (word_count < 3 && iss >> tmp) {
        scmd.push_back(tmp);
        word_count++;
    }
    
    // 读取第4个词（如果存在）
    if (iss >> tmp) {
        scmd.push_back(tmp);
        word_count++;
        
        // 读取剩余所有内容作为最后一个词
        std::string remaining;
        std::getline(iss, remaining);
        
        if (!remaining.empty()) {
            // 删除开头的空格
            remaining = remaining.substr(remaining.find_first_not_of(" \t"));
            if (!remaining.empty()) {
                scmd.push_back(remaining);
            }
        }
    }
}
bool handleClientCmd(std::string cmd, std::string& result) {
    std::vector<std::string> scmd;
    CmdSplit(cmd, scmd);
    std::string cmdType = scmd[0];
    if(cmdType == "C"){
        //创建文件
        if(scmd.size() < 3){
            std::cout << "Invalid command: " << cmdType << std::endl;
            return false;
        }
        std::string cata = scmd[1];
        std::string filename = scmd[2];
        if(fs.createFile(cata,filename,0,0)){
            std::cout << "Create file success: " << filename << std::endl;
            result = "Create file success: " + filename;
            return true;
        }else{
            std::cout << "Create file failed: " << filename << std::endl;
            result = "Create file failed: " + filename;
            return false;
        }
    }else if(cmdType == "D"){
        //删除文件
        if(scmd.size() < 3){
            std::cout << "Invalid command: " << cmdType << std::endl;
            result = "Invalid command: " + cmdType;
            return false;
        }
        std::string cata = scmd[1];
        std::string filename = scmd[2];
        if(fs.deleteFile(cata,filename)){
            std::cout << "Delete file success: " << filename << std::endl;
            result = "Delete file success: " + filename;
            return true;
        }else{
            std::cout << "Delete file failed: " << filename << std::endl;
            result = "Delete file failed: " + filename;
            return false;
        }
        
    }else if(cmdType == "W"){
        //写文件
        if(scmd.size() < 4){
            std::cout << "Invalid command: " << cmdType << std::endl;
            return false;
        }
        std::string cata = scmd[1];
        std::string filename = scmd[2];
        std::string content = scmd[3];
        if(fs.writeFile(cata,filename,content)){
            std::cout << "Write file success: " << filename << std::endl;
            result = "Write file success: " + filename;
            return true;
        }else{
            std::cout << "Write file failed: " << filename << std::endl;
            result = "Write file failed: " + filename;
            return false;
        }

    }else if(cmdType == "R"){
        //读文件
        if(scmd.size() < 3){
            std::cout << "Invalid command: " << cmdType << std::endl;
            return false;
        }
        std::string cata = scmd[1];
        std::string filename = scmd[2];
        std::string content = fs.readFile(cata, filename);
        if(!content.empty()){
            std::cout << "Read file success: " << filename << std::endl;
            std::cout << "Content: " << content << std::endl;
            result = "Read file success: " + filename + "\nContent: " + content;
            return true;
        }else{
            std::cout << "Read file failed: " << filename << std::endl;
            result = "Read file failed: " + filename;
            return false;
        }

    }else if(cmdType == "P"){
        //创建进程
        string path = scmd[1];
        string fileName = scmd[2];
        
        // 将文件信息加入PCB等待队列
        WaitingProcess waitingProcess;
        waitingProcess.path = path;
        waitingProcess.fileName = fileName;
        waitingProcessList.push_back(waitingProcess);
        
        cout << "文件 " << fileName << " 已加入PCB等待队列" << endl;
    

    }else if(cmdType == "B"){
         // 阻塞进程
         int pid = stoi(scmd[1]);

         PCB found;
         bool isFound = false; // 标记是否找到进程

        std::lock_guard<std::mutex> lock(ready_list_mutex);
        readyList0.lock;
        readyList1.lock;
        blockList.lock;
  
         // 查找并移除进程
         for (auto it = readyList0.begin(); it != readyList0.end(); ++it) {
             if (it->pid == pid) {
                 found = *it;
                 readyList0.erase(it);  // 从 readyList0 中移除
                 isFound = true;
                 break;
             }
         }
  
         if (!isFound) {
             for (auto it = readyList1.begin(); it != readyList1.end(); ++it) {
                 if (it->pid == pid) {
                     found = *it;
                     readyList1.erase(it);  // 从 readyList1 中移除
                     isFound = true;
                     break;
                 }
            }
         }
  
            if (isFound) {
                std::cout << "Process with PID " << pid << " has been blocked." << std::endl;
                found.blocktype = SYSTEM;
                found.block_time = time_cnt;
                block(found);  
            }
            else {
                std::cout << "Process does not exist or process can't be blocked." << std::endl;
            }
            std::lock_guard<std::mutex> unlock(ready_list_mutex);
            readyList0.unlock;
            readyList1.unlock;
            blockList.unlock;

    }else if(cmdType == "K"){
        //唤醒进程
        int targetPid = std::stoi(scmd[1]);  // 获取目标pid（假设scmd[1]是一个字符串）
        bool found = false;
        std::lock_guard<std::mutex> lock(ready_list_mutex);
        readyList0.lock;
        readyList1.lock;
        blockList.lock;
        // 遍历阻塞队列，找到目标pid的进程
        for (auto it = blockList.begin(); it != blockList.end(); ++it) {
            PCB* process = *it;
            if (process->pid == targetPid) {
                // 找到了目标进程
                if(process->blocktype == SYSTEM){
                    blockList.erase(it);  // 从阻塞队列中移除该进程
                    process->block_time = 0;
                    ready(it);
                    cout << "process " << process->pid << " has been waken" << endl;
                    found = true;
                    break;
                }
            }
        }
        std::lock_guard<std::mutex> unlock(ready_list_mutex);
        readyList0.unlock;
        readyList1.unlock;
        blockList.unlock;

        if (!found) {
            cout << "Process does not exist or process can't be waken."<< endl;
        }

    }else if(cmdType == "E"){
        //退出系统
        exit(10);
    }
    else{
        std::cout << "Invalid command: " << cmdType << std::endl;
        return false;
    }
    return true;
}

void cpu_worker(CPU& cpu) {
    cpu.running = true;
    while (cpu.running) {
        PCB* current_pcb = nullptr;

        std::lock_guard<std::mutex> lock(ready_list_mutex);
        readyList0.lock();
        readyList1.lock();
        blockList.lock();
        //短期调度
        if (SCHE0 == SCHED_RR) {
        
        } else if (SCHE0 == SCHED_PRO) {
            // 按优先级调度，需要对就绪队列进行排序
            sortReadyListByPriority(readyList0);
        } else if (SCHE0 == SCHED_RRP) {
            // 按响应比调度，需要对就绪队列进行排序
            sortReadyListByResponseRatio(readyList0);
        }
        
        // 根据不同的调度策略调整CPU1的就绪队列
        if (SCHE1 == SCHED_RR) {
            
        } else if (SCHE1 == SCHED_PRO) {
            // 按优先级调度，需要对就绪队列进行排序
            sortReadyListByPriority(readyList1);
        } else if (SCHE1 == SCHED_RRP) {
            // 按响应比调度，需要对就绪队列进行排序
            sortReadyListByResponseRatio(readyList1);
        }
    }
    std::lock_guard<std::mutex> unlock(ready_list_mutex);
    readyList0.unlock();
    readyList1.unlock();
    blockList.unlock();
        
        // 获取就绪进程
        {
            std::lock_guard<std::mutex> lock(ready_list_mutex);
            if(cpu.id==0){
                if (!readyList0.empty()) {
                    current_pcb = &readyList0.front();
                    readyList0.pop_front();
                    cpu.busy = true;
                    cpu.running_process = current_pcb;
                }
            }else{
                if (!readyList1.empty()) {
                    current_pcb = &readyList1.front();
                    readyList1.pop_front();
                    cpu.busy = true;
                    cpu.running_process = current_pcb;
                }
            }
        }
        // 处理当前进程
        if (current_pcb != nullptr) {
            if (current_pcb->has_instruction()) {
                if (current_pcb->current_instruction_time == 1) {
                    // 执行指令
                    //2025.5.11
                    //下面这里的if逻辑有问题
                    //设备函数要到中断才去处理，应该立即将进程阻塞
                    /**old**if (RUN(current_pcb->get_current_instruction())) {
                        //此处成功执行才会继续执行下一条指令
                        //主要是为了设备申请指令能够重复申请，避免一次未成功申请就跳出的情况
                        //只有设备申请失败会返回false以重新申请，其他指令执行失败会触发错误中断处理
                        current_pcb->program.clear();
                    } else {
                        current_pcb->apply_time++;
                        current_pcb->current_instruction_time = 1;
                        if(current_pcb->apply_time>MAX_APPLY_TIME){
                            //此处申请设备次数超过最大次数，处理逻辑待定
                            
                        }
                    }*/

                    RUN(current_pcb->get_current_instruction());
                } else {
                    current_pcb->current_instruction_time--;
                }
            } else {
                // 从内存读取指令
                char instruction_buffer[256] = {0};
                size_t bytes_read = 0;
                bool flag = read_instruction(instruction_buffer, sizeof(instruction_buffer), 
                    current_pcb->address, current_pcb->pid, &bytes_read);
                if (bytes_read > 0) {
                    // 解析指令
                    std::string instruction(instruction_buffer);
                    current_pcb->program = instruction;
        
                    // 更新程序计数器
                    current_pcb->address += bytes_read;
        
                    // 分析指令并分配时间片
                    std::vector<std::string> parts;
                    CmdSplit(instruction, parts);
                    if (parts.size() >= 2) {
                        try {
                            current_pcb->current_instruction_time = std::stoi(parts[1]);
                        } catch (const std::exception& e) {
                            current_pcb->current_instruction_time = 1; // 默认时间片
                        }
                    } else {
                        current_pcb->current_instruction_time = 1; // 默认时间片
                    }
                }

                if (flag==0) {//未运行完，读取命令成功
                    std::lock_guard<std::mutex> lock(ready_list_mutex);
                    //这里的逻辑已经实现了RR，短期调度只需要调整队列内部顺序即可
                    // 进程继续执行，重新加入就绪队列
                    if(cpu.id==0) {
                        readyList0.push_back(*current_pcb);
                    } else {
                        readyList1.push_back(*current_pcb);
                    }
                }else if (flag==-1){//缺页
                    raiseInterrupt(InterruptType::PAGEFAULT, current_pcb->pid,current_pcb->address,"",nullptr,0);
                    if(cpu.id==0) {
                        readyList0.push_back(*current_pcb);
                    } else {
                        readyList1.push_back(*current_pcb);
                    }
                }else{//进程已经运行完毕
                    // 释放当前进程资源
                    stop(current_pcb);
                    current_pcb = nullptr;
                }
                current_pcb->apply_time = 0;
            }
            
            cpu.busy = false;
            cpu.running_process = nullptr;
        }
        
        // 处理中断
        if (!InterruptQueue.empty()) {
            handleInterrupt();
        }
    }
};


//数据处理部分

// 中断类型名称转换
std::string InterruptSystemData::getInterruptTypeName(InterruptType type) {
    switch(type) {
        case InterruptType::TIMER: 
            return "TIMER";
        case InterruptType::DEVICE: 
            return "DEVICE";
        case InterruptType::SOFTWARE: 
            return "SOFTWARE";
        case InterruptType::SNAPSHOT: 
            return "SNAPSHOT";
        case InterruptType::NON_MASKABLE: 
            return "NON_MASKABLE";
        case InterruptType::PAGEFAULT: 
            return "PAGEFAULT";
        case InterruptType::TEST: 
            return "TEST";
        case InterruptType::MERROR: 
            return "ERROR";
        default: 
            return "UNKNOWN";
    }
}

// 处理函数名称转换
std::string InterruptSystemData::getHandlerName(InterruptFunc handler) {
    if(handler == nullptr) 
        return "NULL";
    if(handler == noHandle) 
        return "noHandle";
    if(handler == errorHandle) 
        return "errorHandle";
    if(handler == Pagefault) 
        return "PageFaultHandler";
    if(handler == snapshotSend) 
        return "SnapshotHandler";
    return "UnknownHandler";
}

// 设备类型转换
std::string InterruptSystemData::getDeviceType(int device_id) {
    // 根据设备ID返回对应的设备类型字符串
    switch(device_id) {
        case 0: 
            return "KEYBOARD";
        case 1: 
            return "MOUSE";
        case 2: 
            return "PRINTER";
        case 3: 
            return "DISK";
        default: 
            return "UNKNOWN_DEVICE";
    }
}

// 计算总中断数
int InterruptSystemData::calculateTotalInterrupts() {
    static std::atomic<int> total_interrupts{0};
    return total_interrupts.load();
}

// 获取特定类型中断的触发次数
int InterruptSystemData::getTriggerCount(InterruptType type) {
    static std::map<InterruptType, std::atomic<int>> trigger_counts;
    return trigger_counts[type].load();
}

// 添加计数函数 - 在 raiseInterrupt 函数中调用
void incrementInterruptCount(InterruptType type) {
    static std::map<InterruptType, std::atomic<int>> trigger_counts;
    static std::atomic<int> total_interrupts{0};
    
    trigger_counts[type]++;
    total_interrupts++;
}
