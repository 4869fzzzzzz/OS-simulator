#include "../include/headfile.h"
#include "../include/interrupt.h"

int main(){
    struct test t;
    t.i=10086;
    std::cout<<"hello,OS"<<t.i<<std::endl;
    Interrupt_Init();
}