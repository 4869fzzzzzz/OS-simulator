#include "../include/headfile.h"
#include "../include/interrupt.h"
#include "../include/socket.h"

int main(){
    SetConsoleOutputCP(CP_UTF8);
    struct test t;
    t.i=10086;
    std::cout<<"hello,中文"<<t.i<<std::endl;
    Interrupt_Init();
    /*MySocket serverSocket;
    clientSocket = serverSocket.Accept();
    SOCKADDR_IN client_addr = serverSocket.client_addr;*/
    std::cout<<"主循环开始"<<std::endl;
    while(1){
        
        if(1){

        }
        handleInterrupt();
    }
}