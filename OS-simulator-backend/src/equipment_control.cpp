#define  _CRT_SECURE_NO_WARNINGS 1
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <thread>   // std::this_thread::sleep_for
#include <chrono>   // std::chrono::seconds
#include "../include/device.h"
#include "../include/interrupt.h"


DeviceManager manager;

void Init_Device(){
    createDevices(manager);
}

// 创建所有设备
void createDevices(DeviceManager& manager) {
    manager.createDevice(DeviceType::Disk, "磁盘1");
    alloc_for_device(1,64);
    manager.createDevice(DeviceType::Disk, "磁盘2");
    alloc_for_device(2,64);
    manager.createDevice(DeviceType::Disk, "磁盘3");
    alloc_for_device(3,64);
    manager.createDevice(DeviceType::Printer, "打印机1");
    alloc_for_device(4,64);
    manager.createDevice(DeviceType::Printer, "打印机2");
    alloc_for_device(5,64);
    manager.createDevice(DeviceType::Keyboard, "键盘1");
    alloc_for_device(6,64);
    manager.createDevice(DeviceType::NetworkCard, "网卡1");
    alloc_for_device(7,64);
    manager.createDevice(DeviceType::Other, "其他设备1");
}

// 调用中断请求设备--starttime为pid的阻塞开始时间，如果系统成功分配了设备，则返回当前时间，没有返回-1
void callDeviceInterrupt(int pcb_id, int type, std::string info,int* starttime, int seconds) {
    if(type==8){
        std::lock_guard<std::mutex> lock(blockList_mutex);
        for (auto it = blockList.begin(); it != blockList.end();it++) {
            PCB& P=*it;    
            if(P.pid==pcb_id){
                P.start_block_time=1;//设备使用结束
            }
        }
        blockList_mutex.unlock();
        return;
    }
    else{
        auto device = manager.findAvailableDevice(static_cast<DeviceType>(type));
        if (device) {
            device->enable();
            device->setUsingPid(pcb_id);
            std::cout << "PCB " << pcb_id << " 请求设备: " << device->getName()
                << "，启用中...（" << seconds << "秒）" << std::endl;

            std::thread([device, seconds]() {
                std::this_thread::sleep_for(std::chrono::seconds(seconds));
                device->disable();
                device->clearInterrupt();
                raiseInterrupt(InterruptType::DEVICE, device->getId(), 8, "", nullptr, 0);
                }).detach();

                std::cout << "返回：true，设备ID = " << device->getId() << std::endl;
                cout<<"设备返回到启动时间："<<*starttime<<endl;
        }
        else
        {
            std::cout << "返回：false，wrong（没有可用的该类型设备）" << std::endl;
            *starttime = -1;
        }
    }
}

/*int main() {
    DeviceManager manager;

    createDevices(manager);
    manager.printAllDevices();

    std::cout << "\n==== 模拟调用设备 ====" << std::endl;

    // 示例测试调用
    callDeviceInterrupt(manager, 101, DeviceType::Disk, 3);
    callDeviceInterrupt(manager, 102, DeviceType::Disk, 4);
    callDeviceInterrupt(manager, 103, DeviceType::Disk, 5); // 还有一个磁盘
    callDeviceInterrupt(manager, 104, DeviceType::Disk, 2); // 应该失败（无空闲）

    std::this_thread::sleep_for(std::chrono::seconds(6)); // 等待所有设备完成
    std::cout << "\n==== 最终设备状态 ====" << std::endl;
    manager.printAllDevices();

    return 0;
}*/