#include "../include/headfile.h"
#include "../include/interrupt.h"
#include "../include/socket.h"
#include "../include/process.h"
#include "../include/memory.h"
#include "../include/device.h"


int main(){
    SetConsoleOutputCP(CP_UTF8);
    //socket初始化
    MySocket serverSocket;
    if (!serverSocket.isValid()) {
        std::cerr << "Socket 初始化失败" << std::endl;
        return 1;
    }
    try {
        serverSocket.setNonBlocking(serverSocket.sServer);
    } catch (const std::exception& e) {
        std::cerr << "设置非阻塞模式失败: " << e.what() << std::endl;
        return 1;
    }
    
    CPU cpu0(0),cpu1(1);
    PCB npcb;
    std::thread cpu0_thread(cpu_worker, std::ref(cpu0));//短期调度在该线程内执行
    std::thread cpu1_thread(cpu_worker, std::ref(cpu1));


    //该循环仅用来处理客户端请求以及长期和中期调度
    while(1){
        //处理客户端请求
        serverSocket.HandleConnection();
        //长期调度（取消某些进程申请的内存），创建PCB
        LongTermScheduler(path, filename);//通过给出进程文件的路径以及文件名，创建进程及其PCB，并为其分配内存，如果没有则直接挂起
        //若内存未满，为没有内存的PCB申请内存
        execute();
        //中期调度
        updateTaskState();
        /*
        npcb=readyList.front();
        if(1){//这里修改成，进程有要运行的指令
            if(1)//当前指令剩余所需时间片只剩1；由于指令运行实际只需一个时间片，而这里设定是需要多个时间片因此统一在时间片仅剩1时运行指令
                RUN("CREATE 1 1 1 1");//这里要改成当前PCB要进行的一条指令
            else{
                //指令时间片数减1
            }
        }
        else{//从内存中读入指令-内存模块提供翻译地址的函数方法
            //这里根据内存提供的地址以及偏移量读入指令，指令以\n分割
            //read_memory();这个函数的返回值决定当前进程是否运行完毕
            //如果页未调入，产生缺页中断，在后面中断处理时，进行调页处理
        }*/
        //handleInterrupt();
    }

    cpu0.running = false;
    cpu1.running = false;
    cpu0_thread.join();
    cpu1_thread.join();

    return 0;
}
