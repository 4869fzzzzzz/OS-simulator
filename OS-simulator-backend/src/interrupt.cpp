#include "../include/interrupt.h"
#include "../include/socket.h"


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
    /*std::cout<<std::bitset<16>(valid)<<"中文"<<std::endl;
    if (valid&1<<static_cast<int>(InterruptType::SOFTWARE))
    {
        std::cout<<"有效"<<std::endl;
    }*/
    //初始化中断向量表
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
    InterruptVector Pagefault(noHandle, static_cast<int>(InterruptType::PAGEFAULT));
    InterruptVectorTable[static_cast<int>(InterruptType::PAGEFAULT)]=Pagefault;
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
}

void noHandle(InterruptType type,int p,int q){};
void errorHandle(InterruptType type,int p,int q){
    exit(99);
}
//中断产生
void raiseInterrupt(InterruptType t, int device_id, int value){
    Interrupt itp(t,device_id,value);
    if(handleFlag.load()){

    }else{
        iq.lock();
        InterruptQueue.push(itp);
        iq.unlock();
    } 
}
//中断处理
void handleInterrupt(){
    handleFlag.store(1);
    while(!InterruptQueue.empty()){
        Interrupt tmp=InterruptQueue.top();
        InterruptQueue.pop();
        InterruptVectorTable[static_cast<int>(tmp.type)].handler(tmp.type,tmp.device_id,tmp.value);
    }
    iq.lock();
    while(!readyInterruptQueue.empty()){
        Interrupt tmp=readyInterruptQueue.front();
        InterruptQueue.push(tmp);
        readyInterruptQueue.pop();
    }
    iq.unlock();
    handleFlag.store(0);
}
//工具函数
bool InterruptTool::isValid(InterruptType t){
    return valid.load() & 1<<static_cast<int>(t);
}
bool InterruptTool::setValid(InterruptType t)
{
    valid.store(valid.load()|1<<static_cast<int>(t));
    return true;
}
bool InterruptTool::unsetValid(InterruptType t){
    valid.store(valid.load()&~(1<<static_cast<int>(t)));
    return true;
}
bool InterruptTool::allValid(){
    valid.store(0xffff);
    return true;
}
bool InterruptTool::setPriority(InterruptType t, int mpriority){
    InterruptVectorTable[static_cast<int>(t)].priority=mpriority;
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
            raiseInterrupt(InterruptType::TIMER, 0, 0);
        }
        time_cnt.fetch_add(1);
        if((valid.load()|1<<static_cast<int>(InterruptType::SNAPSHOT))&&time_cnt.load()%10==0){
            raiseInterrupt(InterruptType::SNAPSHOT,0,0);
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

void snapshotSend(InterruptType t,int p,int q){
    std::string frame;
    frame+=std::string(timeToChar(startSysTime));
    frame+=',';
    frame+=std::string(timeToChar(nowSysTime));
    frame+=";";
    std::cout<<frame<<std::endl;

    /*memset(send_buf, 0, sizeof(send_buf));
    strcpy(send_buf,frame.c_str());
    send(clientSocket, send_buf, sizeof(send_buf), 0);*/
}