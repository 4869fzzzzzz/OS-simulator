#include "interrupt_test.h"
#include <iomanip>

std::string getInterruptTypeName(InterruptType type) {
    switch (type) {
        case InterruptType::TIMER: return "时钟中断";
        case InterruptType::DEVICE: return "设备中断";
        case InterruptType::SOFTWARE: return "软件中断";
        case InterruptType::SNAPSHOT: return "快照中断";
        case InterruptType::NON_MASKABLE: return "不可屏蔽中断";
        case InterruptType::PAGEFAULT: return "缺页中断";
        case InterruptType::TEST: return "测试中断";
        case InterruptType::MERROR: return "错误中断";
        default: return "未知中断";
    }
}

InterruptVector InterruptVectorTable[InterruptVectorTableSize]; //中断向量表
std::priority_queue<Interrupt, std::vector<Interrupt>, InterruptComparator> InterruptQueue; //中断处理队列
std::queue<Interrupt> readyInterruptQueue;//存储在中断执行过程中产生的中断
std::mutex iq; //中断队列锁
std::atomic<uint16_t> valid; //valid的每个二进制位代表对应的中断类型是否有效
std::atomic<bool> timerInterruptValid; //时钟中断是否有效标志,时钟在单独的线程运行，这里仅控制时钟是否产生中断，不能控制时钟线程是否存在
std::atomic<bool> stopTimerFlag; //直接停止时钟中断线程
std::atomic<bool> handleFlag;//中断正在处理标志

std::atomic<long long> time_cnt;

time_t startSysTime;
time_t nowSysTime;
std::thread th[2];

void callDeviceInterrupt(int a,int b,std::string c,int* d,int e){
    std::cout<<"触发设备中断，参数："<<a<<std::endl;
}
void Pagefault(int a,int b,std::string c,int*d,int e){
    std::cout<<"触发缺页中断，参数："<<a<<std::endl;
}

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
    Timer.priority=static_cast<int>(InterruptType::TIMER);
    InterruptVectorTable[static_cast<int>(InterruptType::TIMER)]=Timer;
    InterruptVector mDevice(callDeviceInterrupt, static_cast<int>(InterruptType::DEVICE));
    mDevice.priority=static_cast<int>(InterruptType::DEVICE);
    InterruptVectorTable[static_cast<int>(InterruptType::DEVICE)]=mDevice;
    InterruptVector Software(noHandle, static_cast<int>(InterruptType::SOFTWARE));
    Software.priority=static_cast<int>(InterruptType::SOFTWARE);
    InterruptVectorTable[static_cast<int>(InterruptType::SOFTWARE)]=Software;
    InterruptVector Snapshot(noHandle, static_cast<int>(InterruptType::SNAPSHOT));
    Snapshot.priority=static_cast<int>(InterruptType::SNAPSHOT);
    InterruptVectorTable[static_cast<int>(InterruptType::SNAPSHOT)]=Snapshot;
    InterruptVector Non_maskable(noHandle, static_cast<int>(InterruptType::NON_MASKABLE));
    Non_maskable.priority=static_cast<int>(InterruptType::NON_MASKABLE);
    InterruptVectorTable[static_cast<int>(InterruptType::NON_MASKABLE)]=Non_maskable;
    InterruptVector PageFault(Pagefault, static_cast<int>(InterruptType::PAGEFAULT));
    PageFault.priority=static_cast<int>(InterruptType::PAGEFAULT);
    InterruptVectorTable[static_cast<int>(InterruptType::PAGEFAULT)]=PageFault;
    InterruptVector Test(noHandle, static_cast<int>(InterruptType::TEST));
    Test.priority=static_cast<int>(InterruptType::TEST);
    InterruptVectorTable[static_cast<int>(InterruptType::TEST)]=Test;
    InterruptVector Error(errorHandle, static_cast<int>(InterruptType::MERROR));
    Error.priority=static_cast<int>(InterruptType::MERROR);
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
    //exit(99);
    ;
}
//中断产生
void raiseInterrupt(InterruptType t, int v1, int v2, std::string v3,int* v4,int v5){

    //如果中断无效或被屏蔽则不产生中断
    if(!InterruptTool::isValid(t)){
        std::cout<<"中断无效或被屏蔽"<<std::endl;
        return;
    }
    Interrupt itp(t, v1, v2, v3, v4, v5);
    if(handleFlag.load()){
        readyInterruptQueue.push(itp);
    }else{
        std::lock_guard<std::mutex> lock(iq);
        InterruptQueue.push(itp);
        iq.unlock();
    } 
}
//中断处理
void handleInterrupt(){
    
    handleFlag.store(1);
    while(!InterruptQueue.empty()) {
        std::lock_guard<std::mutex> lock(iq);
        if (InterruptQueue.empty()) {
            break;
        }
        Interrupt tmp = InterruptQueue.top();
        InterruptQueue.pop();
        iq.unlock();
        //打印中断信息
        std::cout << "处理中断: " << getInterruptTypeName(tmp.type) << " 中断参数1: " << tmp.value1 << std::endl;

        // 获取原始指针
        int* raw_ptr = tmp.value4 ? tmp.value4.get() : nullptr;
        InterruptVectorTable[static_cast<int>(tmp.type)].handler(
            tmp.value1,
            tmp.value2,
            tmp.value3,
            raw_ptr,  
            tmp.value5
        );
    }
    std::cout<<"处理中断完成"<<std::endl;
    // 打印中断队列状态
    PrintInterruptList();
    std::lock_guard<std::mutex> lock(iq);
    while(!readyInterruptQueue.empty()){
        InterruptQueue.push(std::move(readyInterruptQueue.front()));
        readyInterruptQueue.pop();
    }
    iq.unlock();
    // 减少处理中断的CPU计数
    //interrupt_handling_cpus.store(interrupt_handling_cpus.load()-1);
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
            raiseInterrupt(InterruptType::TIMER, 0, 0, "", nullptr, 0);
        }
        time_cnt.store(time_cnt.load()+1);
        //cout<<time_cnt.load()<<endl;
        if((valid.load()|1<<static_cast<int>(InterruptType::SNAPSHOT))&&time_cnt.load()%10==0){
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


//--------------以下为测试函数------------------


//打印中断处理队列
void PrintInterruptList() {
    std::lock_guard<std::mutex> lock(iq);
    
    std::cout << "\n============ 中断队列状态 ============" << std::endl;
    if (InterruptQueue.empty()) {
        std::cout << "当前中断队列为空" << std::endl;
    } else {
        // 创建临时队列用于遍历
        auto tempQueue = InterruptQueue;
        int index = 0;
        
        std::cout << "当前队列中的中断：" << std::endl;
        std::cout << std::left << std::setw(5) << "序号" 
                  << std::setw(15) << "中断类型"
                  << std::setw(10) << "优先级"
                  << std::setw(20) << "触发时间"
                  << std::setw(30) << "参数" << std::endl;
        
        while (!tempQueue.empty()) {
            const auto& interrupt = tempQueue.top();
            
            // 获取中断类型名称
            std::string typeName;
            switch (interrupt.type) {
                case InterruptType::TIMER: typeName = "时钟中断"; break;
                case InterruptType::DEVICE: typeName = "设备中断"; break;
                case InterruptType::SOFTWARE: typeName = "软件中断"; break;
                case InterruptType::SNAPSHOT: typeName = "快照中断"; break;
                case InterruptType::NON_MASKABLE: typeName = "不可屏蔽中断"; break;
                case InterruptType::PAGEFAULT: typeName = "缺页中断"; break;
                case InterruptType::TEST: typeName = "测试中断"; break;
                case InterruptType::MERROR: typeName = "错误中断"; break;
                default: typeName = "未知中断"; break;
            }
            
            // 格式化参数字符串
            std::stringstream params;
            params << "v1=" << interrupt.value1
                   << ", v2=" << interrupt.value2
                   << ", v3=" << interrupt.value3;
            
            // 打印中断信息
            std::cout << std::left << std::setw(5) << index++
                      << std::setw(15) << typeName
                      << std::setw(10) << InterruptVectorTable[static_cast<int>(interrupt.type)].priority
                      << std::setw(20) << interrupt.timecount
                      << std::setw(30) << params.str() << std::endl;
            
            tempQueue.pop();
        }
    }
    
    // 打印就绪中断队列状态
    std::cout << "\n就绪中断队列状态：" << std::endl;
    if (readyInterruptQueue.empty()) {
        std::cout << "当前就绪中断队列为空" << std::endl;
    } else {
        auto tempReadyQueue = readyInterruptQueue;
        int readyIndex = 0;
        while (!tempReadyQueue.empty()) {
            const auto& interrupt = tempReadyQueue.front();
            std::cout << "就绪中断 " << readyIndex++ 
                      << ": 类型=" << getInterruptTypeName(interrupt.type)
                      << ", 时间=" << interrupt.timecount << std::endl;
            tempReadyQueue.pop();
        }
    }
    
    std::cout << "====================================\n" << std::endl;
}
//打印中断向量表
void PrintInterruptTable() {
    std::cout << "\n============ 中断向量表 ============" << std::endl;
    std::cout << std::left 
              << std::setw(5) << "序号"
              << std::setw(15) << "中断类型"
              << std::setw(10) << "优先级"
              << std::setw(15) << "状态"
              << std::setw(20) << "处理函数地址" << std::endl;
    
    for (int i = 0; i < InterruptVectorTableSize; ++i) {
        // 获取中断类型名称
        std::string typeName;
        switch (static_cast<InterruptType>(i)) {
            case InterruptType::TIMER: typeName = "时钟中断"; break;
            case InterruptType::DEVICE: typeName = "设备中断"; break;
            case InterruptType::SOFTWARE: typeName = "软件中断"; break;
            case InterruptType::SNAPSHOT: typeName = "快照中断"; break;
            case InterruptType::NON_MASKABLE: typeName = "不可屏蔽中断"; break;
            case InterruptType::PAGEFAULT: typeName = "缺页中断"; break;
            case InterruptType::TEST: typeName = "测试中断"; break;
            case InterruptType::MERROR: typeName = "错误中断"; break;
            default: typeName = "未知中断"; break;
        }

        // 获取中断状态
        std::string status;
        if (static_cast<InterruptType>(i) >= InterruptType::NON_MASKABLE) {
            status = "不可屏蔽";
        } else {
            status = InterruptTool::isValid(static_cast<InterruptType>(i)) ? "启用" : "禁用";
        }

        // 获取处理函数地址
        std::stringstream handlerAddr;
        handlerAddr << std::hex << std::showbase 
                   << reinterpret_cast<uintptr_t>(InterruptVectorTable[i].handler);

        // 打印中断向量信息
        std::cout << std::left 
                  << std::setw(5) << i
                  << std::setw(15) << typeName
                  << std::setw(10) << InterruptVectorTable[i].priority
                  << std::setw(15) << status
                  << std::setw(20) << handlerAddr.str() << std::endl;
    }
    
    // 打印时钟中断特殊状态
    std::cout << "\n时钟中断状态：" 
              << (timerInterruptValid.load() ? "启用" : "禁用") << std::endl;
    std::cout << "时钟线程状态：" 
              << (stopTimerFlag.load() ? "已停止" : "运行中") << std::endl;
    
    std::cout << "====================================\n" << std::endl;
}

//中断测试程序
int main(){
    Interrupt_Init();
    int running_flag=1;
    std::string cmd="";
    std::cout<<"==========指令参考==========="<<std::endl;
    std::cout<<"p  : 打印中断向量表和处理队列"<<std::endl;
    std::cout<<"e  : 退出测试程序"<<std::endl;
    std::cout<<"r  : 产生中断 (需要输入中断类型)"<<std::endl;
    std::cout<<"s  : 设置中断优先级 (需要输入中断类型和优先级)"<<std::endl;
    std::cout<<"v  : 设置中断有效性"<<std::endl;
    std::cout<<"uv : 取消中断有效性"<<std::endl;
    std::cout<<"et : 启用时钟中断"<<std::endl;
    std::cout<<"dt : 禁用时钟中断"<<std::endl;
    std::cout<<"st : 停止时钟中断"<<std::endl;
    std::cout<<"pt : 打印时钟和当前系统时间"<<std::endl;
    std::cout<<"h  : 处理当前队列中的所有中断"<<std::endl;
    std::cout<<"=========================="<<std::endl;
    while(running_flag){
        
        while(1){
            std::cout<<"请输入指令："<<std::endl;
            std::cin>>cmd;
            if(cmd=="p"){//打印中断向量表和处理队列
                PrintInterruptList();
                PrintInterruptTable();
            }
            else if(cmd=="e"){//退出测试程序
                running_flag=0;
                break;
            }
            else if(cmd=="r"){//产生中断
                std::string type;
                std::cin>>type;
                if(type=="TIMER"){
                    raiseInterrupt(InterruptType::TIMER, 0, 0, "", nullptr, 0);
                }else if(type=="DEVICE"){
                    raiseInterrupt(InterruptType::DEVICE, 0, 0, "", nullptr, 0);
                }else if(type=="SOFTWARE"){
                    raiseInterrupt(InterruptType::SOFTWARE, 0, 0, "", nullptr, 0);
                }else if(type=="SNAPSHOT"){
                    raiseInterrupt(InterruptType::SNAPSHOT, 0, 0, "", nullptr, 0);
                }else if(type=="NON_MASKABLE"){
                    raiseInterrupt(InterruptType::NON_MASKABLE, 0, 0, "", nullptr, 0);
                }else if(type=="PAGEFAULT"){
                    raiseInterrupt(InterruptType::PAGEFAULT, 0, 0, "", nullptr, 0);
                }else if(type=="TEST"){
                    raiseInterrupt(InterruptType::TEST, 0, 0, "", nullptr, 0);
                }else if(type=="MERROR"){
                    raiseInterrupt(InterruptType::MERROR, 0, 0, "", nullptr, 0);
                }else{
                    std::cout<<"输入错误"<<std::endl;
                }
            }
            else if(cmd=="s"){//设置中断优先级
                std::cout<<"请输入中断类型和优先级"<<std::endl;
                std::string type;
                int priority;
                std::cin>>type>>priority;
                if(type=="TIMER"){
                    InterruptTool::setPriority(InterruptType::TIMER,priority);
                }else if(type=="DEVICE"){
                    InterruptTool::setPriority(InterruptType::DEVICE,priority);
                }else if(type=="SOFTWARE"){
                    InterruptTool::setPriority(InterruptType::SOFTWARE,priority);
                }else if(type=="SNAPSHOT"){
                    InterruptTool::setPriority(InterruptType::SNAPSHOT,priority);
                }else if(type=="NON_MASKABLE"){
                    InterruptTool::setPriority(InterruptType::NON_MASKABLE,priority);
                }else if(type=="PAGEFAULT"){
                    InterruptTool::setPriority(InterruptType::PAGEFAULT,priority);
                }else if(type=="TEST"){
                    InterruptTool::setPriority(InterruptType::TEST,priority);
                }else if(type=="MERROR"){
                    InterruptTool::setPriority(InterruptType::MERROR,priority);
                }else{
                    std::cout<<"输入错误"<<std::endl;
                }
            }
            else if(cmd=="v"){//设置中断有效性
                std::cout<<"请输入中断类型"<<std::endl;
                std::string type;
                std::cin>>type;
                if(type=="TIMER"){
                    InterruptTool::setValid(InterruptType::TIMER);
                }else if(type=="DEVICE"){
                    InterruptTool::setValid(InterruptType::DEVICE);
                }else if(type=="SOFTWARE"){
                    InterruptTool::setValid(InterruptType::SOFTWARE);
                }else if(type=="SNAPSHOT"){
                    InterruptTool::setValid(InterruptType::SNAPSHOT);
                }else if(type=="NON_MASKABLE"){
                    InterruptTool::setValid(InterruptType::NON_MASKABLE);
                }else if(type=="PAGEFAULT"){
                    InterruptTool::setValid(InterruptType::PAGEFAULT);
                }else if(type=="TEST"){
                    InterruptTool::setValid(InterruptType::TEST);
                }else if(type=="MERROR"){
                    InterruptTool::setValid(InterruptType::MERROR);
                }else{
                    std::cout<<"输入错误"<<std::endl;
                }
            }
            else if(cmd=="uv"){//取消中断有效性
                std::cout<<"请输入中断类型"<<std::endl;
                std::string type;
                std::cin>>type;
                if(type=="TIMER"){
                    InterruptTool::unsetValid(InterruptType::TIMER);
                }else if(type=="DEVICE"){
                    InterruptTool::unsetValid(InterruptType::DEVICE);
                }else if(type=="SOFTWARE"){
                    InterruptTool::unsetValid(InterruptType::SOFTWARE);
                }else if(type=="SNAPSHOT"){
                    InterruptTool::unsetValid(InterruptType::SNAPSHOT);
                }else if(type=="NON_MASKABLE"){
                    InterruptTool::unsetValid(InterruptType::NON_MASKABLE);
                }else if(type=="PAGEFAULT"){
                    InterruptTool::unsetValid(InterruptType::PAGEFAULT);
                }else if(type=="TEST"){
                    InterruptTool::unsetValid(InterruptType::TEST);
                }else if(type=="MERROR"){
                    InterruptTool::unsetValid(InterruptType::MERROR);
                }else{
                    std::cout<<"输入错误"<<std::endl;
                }
            }
            else if(cmd=="et"){//启用时钟中断
                InterruptTool::enableTimerInterrupt();
            }
            else if(cmd=="dt"){//禁用时钟中断
                InterruptTool::disableTimerInterrupt();
            }
            else if(cmd=="st"){//停止时钟中断
                InterruptTool::stopTimer();
            }
            else if(cmd=="pt"){//打印时钟和当前系统时间
                std::cout<<"时钟中断状态："<<(timerInterruptValid.load()?"启用":"禁用")<<std::endl;
                std::cout<<"时钟线程状态："<<(stopTimerFlag.load()?"已停止":"运行中")<<std::endl;
                std::cout<<"时钟中断计数："<<time_cnt.load()<<std::endl;
                std::cout<<"系统时间："<<ctime(&nowSysTime)<<std::endl;
                std::cout<<"系统启动时间："<<ctime(&startSysTime)<<std::endl;
            }
            else if(cmd=="h"){//处理中断
                break;
            }
            else{
                std::cout<<"输入错误"<<std::endl;
                std::cout<<"==========指令参考==========="<<std::endl;
                std::cout<<"p  : 打印中断向量表和处理队列"<<std::endl;
                std::cout<<"e  : 退出测试程序"<<std::endl;
                std::cout<<"r  : 产生中断 (需要输入中断类型)"<<std::endl;
                std::cout<<"s  : 设置中断优先级 (需要输入中断类型和优先级)"<<std::endl;
                std::cout<<"v  : 设置中断有效性"<<std::endl;
                std::cout<<"uv : 取消中断有效性"<<std::endl;
                std::cout<<"et : 启用时钟中断"<<std::endl;
                std::cout<<"dt : 禁用时钟中断"<<std::endl;
                std::cout<<"st : 停止时钟中断"<<std::endl;
                std::cout<<"pt : 打印时钟和当前系统时间"<<std::endl;
                std::cout<<"h  : 处理当前队列中的所有中断"<<std::endl;
                std::cout<<"=========================="<<std::endl;
            }
        }
        PrintInterruptList();
        PrintInterruptTable();
        std::cout<<"输入任意内容开始处理中断"<<std::endl;
        // 处理中断
        std::cin>>cmd;
        handleInterrupt();
        
    }
    return 0;
}