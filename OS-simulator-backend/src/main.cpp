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
            
        }
        handleInterrupt();
    }
}