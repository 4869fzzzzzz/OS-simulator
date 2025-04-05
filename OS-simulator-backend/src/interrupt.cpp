#include "../include/interrupt.h"
#include "interrupt.h"


InterruptVector InterruptVectorTable[InterruptVectorTableSize]; //中断向量表
std::mutex iq; //中断队列锁
std::atomic<uint16_t> valid; //valid的每个二进制位代表对应的中断类型是否有效
std::atomic<bool> timerInterruptValid; //时钟中断是否有效标志,时钟在单独的线程运行，这里仅控制时钟是否产生中断，不能控制时钟线程是否存在
std::atomic<bool> stopTimerFlag; //直接停止时钟中断线程
std::atomic<bool> handleFlag;
std::priority_queue<Interrupt, std::vector<Interrupt>, InterruptComparator> InterruptQueue; //中断等待队列
std::queue<Interrupt> readyInterruptQueue;//存储在中断执行过程中产生的中断

void Interrupt_Init(){ //中断初始化
    //初始化中断有效位
    valid=0x0000|1<<static_cast<int>(InterruptType::TIMER)
    |1<<static_cast<int>(InterruptType::DEVICE)
    |1<<static_cast<int>(InterruptType::SOFTWARE)
    |1<<static_cast<int>(InterruptType::NON_MASKABLE)
    |1<<static_cast<int>(InterruptType::PAGEFAULT)
    |1<<static_cast<int>(InterruptType::TEST)
    |1<<static_cast<int>(InterruptType::ERROR);
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
    InterruptVector Non_maskable(noHandle, static_cast<int>(InterruptType::NON_MASKABLE));
    InterruptVectorTable[static_cast<int>(InterruptType::NON_MASKABLE)]=Non_maskable;
    InterruptVector Pagefault(noHandle, static_cast<int>(InterruptType::PAGEFAULT));
    InterruptVectorTable[static_cast<int>(InterruptType::PAGEFAULT)]=Pagefault;
    InterruptVector Test(noHandle, static_cast<int>(InterruptType::TEST));
    InterruptVectorTable[static_cast<int>(InterruptType::TEST)]=Test;
    InterruptVector Error(errorHandle, static_cast<int>(InterruptType::ERROR));
    InterruptVectorTable[static_cast<int>(InterruptType::ERROR)]=Error;
    //清空中断处理队列
    while(!InterruptQueue.empty())
        InterruptQueue.pop();
    while(!readyInterruptQueue.empty())
        readyInterruptQueue.pop();
    //设置标志位
    handleFlag.store(0);
    stopTimerFlag.store(0);
    timerInterruptValid.store(1);
}

void noHandle(InterruptType type,int p,int q){};
void errorHandle(InterruptType type,int p,int q){
    exit(99);
}

void raiseInterrupt(InterruptType t, int device_id, int value){
    Interrupt itp(t,device_id,value);
    if(handleFlag.load()){

    }else{
        iq.lock();
        InterruptQueue.push(itp);
        iq.unlock();
    } 
}

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