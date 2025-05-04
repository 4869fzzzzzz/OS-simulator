#include "../include/headfile.h"
#include "../include/interrupt.h"
#include "../include/socket.h"
#include "../include/process.h"
#include "../include/memory.h"
#include "../include/device.h"


int main(){
    SetConsoleOutputCP(CP_UTF8);
    /*MySocket serverSocket;
    clientSocket = serverSocket.Accept();
    SOCKADDR_IN client_addr = serverSocket.client_addr;*/
    std::cout<<"主循环开始"<<std::endl;
    PCB npcb;
    while(1){
        //创建进程PCB，长期调度（取消某些进程申请的内存）

        //若内存未满，为没有内存的PCB申请内存

        //中期调度，选出要运行的PCB

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
        }
        handleInterrupt();
    }
}