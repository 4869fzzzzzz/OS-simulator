#include "../include/client.h"

void snapshotSend(InterruptType t,int p,int q){
    std::string frame;
    frame+=std::string(timeToChar(get_startSysTime()));
    frame+=',';
    frame+=std::string(timeToChar(get_nowSysTime()));
    frame+=";";
    std::cout<<frame<<std::endl;

    /*memset(send_buf, 0, sizeof(send_buf));
    strcpy(send_buf,frame.c_str());
    send(clientSocket, send_buf, sizeof(send_buf), 0);*/
}