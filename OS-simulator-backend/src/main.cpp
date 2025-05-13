#include "../include/headfile.h"
#include "../include/process.h"
#include "../include/interrupt.h"
#include "../include/socket.h"
#include "../include/memory.h"
#include "../include/device.h"
#include "../include/filesystem.h"



//待办
//1.添加创建目录操作，删除目录操作
//2.添加传输消息格式，给前端传输文件方法
//3.多行代码的传输和处理问题


int main(){
    SetConsoleOutputCP(CP_UTF8);
    //socket初始化
    
#if SOCKETBEGIN
    
    /*if (!serverSocket.isValid()) {
        std::cerr << "Socket 初始化失败" << std::endl;
        return 1;
    }
    try {
        serverSocket.setNonBlocking(serverSocket.sServer);
    } catch (const std::exception& e) {
        std::cerr << "设置非阻塞模式失败: " << e.what() << std::endl;
        return 1;
    }*/
    
    std::thread recv_thread(RecvThread);
    while(!serverSocket.acceptflag){}
#else   
    /*std::vector<string> tcmd;
    tcmd.push("")
    handleClientCmd();*/
#endif 
    
    PCB npcb;
    Interrupt_Init(); 
    init_memory();
    Init_Device();
    std::thread cpu0_thread(cpu_worker, std::ref(cpu0));//短期调度在该线程内执行
    std::thread cpu1_thread(cpu_worker, std::ref(cpu1));


    //该循环仅用来处理客户端请求以及长期和中期调度
    while(1){
        //处理客户端请求
        #if SOCKETBEGIN
        //长期调度（取消某些进程申请的内存），创建PCB
        //此处在遍历检查阻塞队列时，如果有设备申请的进程成功申请到设备，要检查其占用时间是否已过，
        //如果已经过了，就将其从阻塞队列中移除，并且去除当前运行指令
        //LongTermScheduler(path, filename);//通过给出进程文件的路径以及文件名，创建进程及其PCB，并为其分配内存，如果没有则直接挂起
        
        #endif
        CreatePCB();//给没有pcb的进程创建pcb并添加到挂起队列
        //若内存未满，为没有内存的PCB申请内存
        AllocateMemoryForPCB();
        //execute();
        //中期调度
        //updateTaskState();
        MidStageScheduler();//中期调度程序
        

        //handleInterrupt();
    }

    cpu0.running = false;
    cpu1.running = false;
    cpu0_thread.join();
    cpu1_thread.join();

    return 0;
}
