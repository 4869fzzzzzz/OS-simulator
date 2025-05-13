#pragma once
#include "headfile.h"
#include "interrupt.h"
class SystemSnapshot {
public:
    TimerData timer;
    ProcessSystemStatusForUI process;
    InterruptSystemData interrupt;

    AIGC_JSON_HELPER(timer, process, interrupt)
    AIGC_JSON_HELPER_RENAME("timer", "process", "interrupt")
};
void snapshotSend(int v1,int v2,std::string v3,int* v4, int v5);

