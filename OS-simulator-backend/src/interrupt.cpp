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
    InterruptVector mDevice(callDeviceInterrupt, static_cast<int>(InterruptType::DEVICE));
    InterruptVectorTable[static_cast<int>(InterruptType::DEVICE)]=mDevice;
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
    timerInterruptValid.store(1);
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
        std::lock_guard<std::mutex> lock(iq);
        InterruptQueue.push(itp);
       
    } 
}
//中断处理
void handleInterrupt(){
    // 增加处理中断的CPU计数
    std::cout<<"处理中断"<<std::endl;
    interrupt_handling_cpus++;
    // 等待另一个CPU也进入中断处理
    while (interrupt_handling_cpus < 2) {
        std::this_thread::yield();
    }
    
    handleFlag.store(1);
    while(!InterruptQueue.empty()){
        std::lock_guard<std::mutex> lock(iq);
        if (InterruptQueue.empty()) {
            return;
        }
        Interrupt tmp=InterruptQueue.top();
        InterruptQueue.pop();
        
        InterruptVectorTable[static_cast<int>(tmp.type)].handler(tmp.value1,tmp.value2,tmp.value3,tmp.value4,tmp.value5);
    }
    std::lock_guard<std::mutex> lock(iq);
    while(!readyInterruptQueue.empty()){
        Interrupt tmp=readyInterruptQueue.front();
        InterruptQueue.push(tmp);
        readyInterruptQueue.pop();
    }
    
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
        //std::cout << "时钟 thread running..." << std::endl; // 调试输出
        if (timerInterruptValid.load()) {
            std::cout<<"产生时钟中断"<<std::endl;
            raiseInterrupt(InterruptType::TIMER, 0, 0, "", nullptr, 0);
        }
        time_cnt.fetch_add(1);
        
        if((valid.load()|1<<static_cast<int>(InterruptType::SNAPSHOT))&&time_cnt.load()%10==0){
        #if SOCKETBEGIN
            raiseInterrupt(InterruptType::SNAPSHOT, 0, 0, "", nullptr, 0);
        #else
            
        #endif
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
        return true;
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
                return true;
            }else{
               //产生设备中断，后续补充
                int devicetype=stoi(scmd[2]);
                int needtime=stoi(scmd[3]);
                
                raiseInterrupt(InterruptType::DEVICE,current_pcb->pid,devicetype,"",&current_pcb->start_block_time,needtime*10);
                return false;
            }

        }else if(cmdType=="OUTPUT"){
            if(scmd.size()<3){
                raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
                return true;
            }else{
                //产生设备中断，后续补充
                int devicetype=stoi(scmd[2]);
                int needtime=stoi(scmd[3]);
                raiseInterrupt(InterruptType::DEVICE,current_pcb->pid,devicetype,"",&current_pcb->start_block_time,needtime*10);
                return false;
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
                int tpid = stoi(scmd[2]);
                PCB* found = nullptr;
                bool isFound = false;

                // 使用互斥锁保护就绪队列操作
                std::lock_guard<std::mutex> lock(ready_list_mutex);
                {
                    // 先从 readyList0 查找
                    for (auto it = readyList0.begin(); it != readyList0.end(); ++it) {
                        if (it->pid == tpid) {
                            found = new PCB(*it); // 创建副本
                            readyList0.erase(it);
                            isFound = true;
                            break;
                        }
                    }

                    // 如果在readyList0中未找到，则查找readyList1
                    if (!isFound) {
                        for (auto it = readyList1.begin(); it != readyList1.end(); ++it) {
                            if (it->pid == tpid) {
                                found = new PCB(*it); // 创建副本
                                readyList1.erase(it);
                                isFound = true;
                                break;
                            }
                        }
                    }
                }
                

                if (isFound && found != nullptr) {
                    // 更新进程状态
                    found->state = BLOCK;
                    found->blocktype = SYSTEMB;
                    found->start_block_time = time_cnt.load();
                    
                    // 使用互斥锁保护阻塞队列操作
                    std::lock_guard<std::mutex> lock(blockList_mutex);
                    blockList.push_back(*found);
                    
                    
                    delete found; // 释放临时对象
                    std::cout << "[INFO] Process " << tpid << " has been blocked successfully." << std::endl;
                } else {
                    std::cout << "[ERROR] Process " << tpid << " not found in ready queues." << std::endl;
                }
                return true;
            }

        }else if(cmdType=="WAKE"){
            if(scmd.size()<3){
                raiseInterrupt(InterruptType::MERROR,0,0,"",nullptr,0);
                return true;
            }else{
                int tpid = stoi(scmd[2]);  
                PCB found;
                bool isFound = false;

                // 1. 先在阻塞队列中查找
                std::lock_guard<std::mutex> lock(blockList_mutex);
                for (auto it = blockList.begin(); it != blockList.end(); ++it) {
                    if (it->pid == tpid) {
                        found = *it;
                        blockList.erase(it);
                        isFound = true;
                        std::cout << "[INFO] Found process " << tpid << " in block list" << std::endl;
                        break;
                    }
                }
                

                // 2. 如果阻塞队列中未找到，则在挂起队列中查找
                if (!isFound) {
                    std::lock_guard<std::mutex> lock(suspendList_mutex);
                    for (auto it = suspendList.begin(); it != suspendList.end(); ++it) {
                        if (it->pid == tpid) {
                            found = *it;
                            suspendList.erase(it);
                            isFound = true;
                            std::cout << "[INFO] Found process " << tpid << " in suspend list" << std::endl;
                            break;
                        }
                    }
                    
                    
                    // 如果是挂起进程，需要重新分配内存
                    if (isFound) {
                        if (page_in(found.address, found.pid)) {
                            std::cout << "[INFO] Successfully allocated memory for process " << tpid << std::endl;
                        } else {
                            std::cout << "[ERROR] Failed to allocate memory for process " << tpid << std::endl;
                            std::lock_guard<std::mutex> lock(suspendList_mutex);
                            suspendList.push_back(found);
                            
                            return true;
                        }
                    }
                }

                if (isFound) {
                    // 更新进程状态并加入就绪队列
                    found.state = READY;
                    found.blocktype = NOTBLOCK;
                    found.start_block_time = 0;
                    ready(found);
                    std::cout << "[INFO] Process " << tpid << " has been waked up successfully" << std::endl;
                    return true;
                } else {
                    std::cout << "[ERROR] Process " << tpid << " not found in block or suspend list" << std::endl;
                }
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
        if(fs.createFile(cata,filename,0,0)==0){
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
        if(fs.deleteFile(cata,filename)==0){
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
        if(fs.writeFile(cata,filename,content)==0){
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
        //LongTermScheduler(scmd[1], scmd[2]);
       pPCB newProcess;
       newProcess.path = scmd[1];
       newProcess.filename = scmd[2];
       std::lock_guard<std::mutex> lock(prePCBList_mutex);
       prePCBList.push_back(newProcess);
       result = "Create Process";
       std::cout << "成功创建待创建进程" << newProcess.filename << std::endl;

    }// 在 handleClientCmd 函数中替换原有的 "B" 和 "K" 分支
    else if(cmdType == "B"){
        // 阻塞进程
        if(scmd.size() < 2) {
            std::cout << "Invalid command: " << cmdType << std::endl;
            result = "Invalid command: Missing process ID";
            return false;
        }
        int tpid = stoi(scmd[1]);
        PCB* found = nullptr;
        bool isFound = false;

        // 使用互斥锁保护就绪队列操作
        std::lock_guard<std::mutex> lock(ready_list_mutex);
        {
            // 先从 readyList0 查找
            for (auto it = readyList0.begin(); it != readyList0.end(); ++it) {
                if (it->pid == tpid) {
                    found = new PCB(*it);
                    readyList0.erase(it);
                    isFound = true;
                    break;
                }
            }

            // 如果在readyList0中未找到，则查找readyList1
            if (!isFound) {
                for (auto it = readyList1.begin(); it != readyList1.end(); ++it) {
                    if (it->pid == tpid) {
                        found = new PCB(*it);
                        readyList1.erase(it);
                        isFound = true;
                        break;
                    }
                }
            }
        }
        

        if (isFound && found != nullptr) {
            // 更新进程状态
            found->state = BLOCK;
            found->blocktype = SYSTEMB;
            found->start_block_time = time_cnt.load();
            
            // 使用互斥锁保护阻塞队列操作
            std::lock_guard<std::mutex> lock(blockList_mutex);
            blockList.push_back(*found);
            
            delete found;
            result = "[INFO] Process " + to_string(tpid) + " has been blocked successfully.";
            std::cout << result << std::endl;
            return true;
        } else {
            result = "[ERROR] Process " + to_string(tpid) + " not found in ready queues.";
            std::cout << result << std::endl;
            return false;
        }
    }
    else if(cmdType == "K"){
        // 唤醒进程
        if(scmd.size() < 2) {
            std::cout << "Invalid command: " << cmdType << std::endl;
            result = "Invalid command: Missing process ID";
            return false;
        }
        
        int tpid = stoi(scmd[1]);
        PCB found;
        bool isFound = false;

        // 先在阻塞队列中查找
        std::lock_guard<std::mutex> lock(blockList_mutex);
        for (auto it = blockList.begin(); it != blockList.end(); ++it) {
            if (it->pid == tpid) {
                found = *it;
                blockList.erase(it);
                isFound = true;
                std::cout << "[INFO] Found process " << tpid << " in block list" << std::endl;
                break;
            }
        }
        

        // 如果阻塞队列中未找到，则在挂起队列中查找
        if (!isFound) {
            std::lock_guard<std::mutex> lock(suspendList_mutex);
            for (auto it = suspendList.begin(); it != suspendList.end(); ++it) {
                if (it->pid == tpid) {
                    found = *it;
                    suspendList.erase(it);
                    isFound = true;
                    std::cout << "[INFO] Found process " << tpid << " in suspend list" << std::endl;
                    break;
                }
            }
            
            
            // 如果是挂起进程，需要重新分配内存
            if (isFound) {
                if (page_in(found.address, found.pid)) {
                    std::cout << "[INFO] Successfully allocated memory for process " << tpid << std::endl;
                } else {
                    std::cout << "[ERROR] Failed to allocate memory for process " << tpid << std::endl;
                    std::lock_guard<std::mutex> lock(suspendList_mutex);
                    suspendList.push_back(found);
                    result = "[ERROR] Failed to allocate memory for process " + to_string(tpid);
                    return false;
                }
            }
        }

        if (isFound) {
            // 更新进程状态并加入就绪队列
            found.state = READY;
            found.blocktype = NOTBLOCK;
            found.start_block_time = 0;
            ready(found);
            result = "[INFO] Process " + to_string(tpid) + " has been waked up successfully";
            std::cout << result << std::endl;
            return true;
        } else {
            result = "[ERROR] Process " + to_string(tpid) + " not found in block or suspend list";
            std::cout << result << std::endl;
            return false;
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
        //std::cout<<"running"<<std::endl;
        PCB* current_pcb = nullptr;
        //短期调度
        shortScheduler();  
        // 获取就绪进程
        std::lock_guard<std::mutex> lock(ready_list_mutex);
        {
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
        
        int pcb_exist_flag=1;
        // 处理当前进程
        if (current_pcb != nullptr) {
            if (current_pcb->has_instruction()) {
                if (current_pcb->current_instruction_time == 1) {
                    if(!RUN(current_pcb->get_current_instruction(),current_pcb)){
                        current_pcb->state=BLOCK;
                        current_pcb->blocktype=DEVICEB;
                        std::lock_guard<std::mutex> lock(blockList_mutex);
                        blockList.push_back(*current_pcb);
                        
                        pcb_exist_flag=0;
                    }
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
                    std::string newinstruction(instruction_buffer);
                    current_pcb->instruction = newinstruction;
        
                    // 更新程序计数器
                    current_pcb->address += bytes_read;
        
                    // 分析指令并分配时间片
                    std::vector<std::string> parts;
                    CmdSplit(newinstruction, parts);
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
                    //这里的逻辑已经实现了RR，短期调度只需要调整队列内部顺序即可
                    // 进程继续执行，重新加入就绪队列
                }else if (flag==-1){//缺页
                    raiseInterrupt(InterruptType::PAGEFAULT, current_pcb->pid,current_pcb->address,"",nullptr,0);
                }else{//进程已经运行完毕
                    // 释放当前进程资源
                    pcb_exist_flag=0;
                    delete current_pcb;
                    current_pcb = nullptr;
                }
                current_pcb->apply_time = 0;
            }
            if(pcb_exist_flag){
                std::lock_guard<std::mutex> lock(ready_list_mutex);
                if(cpu.id==0) {
                    readyList0.push_back(*current_pcb);
                } else {
                    readyList1.push_back(*current_pcb);
                }
                
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