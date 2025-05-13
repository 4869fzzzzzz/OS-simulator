#define  _CRT_SECURE_NO_WARNINGS 1
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <thread>   // std::this_thread::sleep_for
#include <chrono>   // std::chrono::seconds
#include "../include/device.h"



DeviceManager manager;

void Init_Device(){
    createDevices(manager);
}

// ���������豸
void createDevices(DeviceManager& manager) {
    manager.createDevice(DeviceType::Disk, "����1");
    alloc_for_device(1,64);
    manager.createDevice(DeviceType::Disk, "����2");
    alloc_for_device(2,64);
    manager.createDevice(DeviceType::Disk, "����3");
    alloc_for_device(3,64);
    manager.createDevice(DeviceType::Printer, "��ӡ��1");
    alloc_for_device(4,64);
    manager.createDevice(DeviceType::Printer, "��ӡ��2");
    alloc_for_device(5,64);
    manager.createDevice(DeviceType::Keyboard, "����1");
    alloc_for_device(6,64);
    manager.createDevice(DeviceType::NetworkCard, "����1");
    alloc_for_device(7,64);
    manager.createDevice(DeviceType::Other, "�����豸1");
}

// �����ж������豸--starttimeΪpid��������ʼʱ�䣬���ϵͳ�ɹ��������豸���򷵻ص�ǰʱ�䣬û�з���-1
void callDeviceInterrupt(int pcb_id, int type, std::string info,int* starttime, int seconds) {
    auto device = manager.findAvailableDevice(static_cast<DeviceType>(type));
    if (device) {
        device->enable();
        std::cout << "PCB " << pcb_id << " �����豸: " << device->getName()
            << "��������...��" << seconds << "�룩" << std::endl;

        std::thread([device, seconds]() {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            device->disable();
            device->clearInterrupt();
            }).detach();

            std::cout << "���أ�true���豸ID = " << device->getId() << std::endl;
            *starttime = time_cnt.load();
    }
    std::cout << "���أ�false��wrong��û�п��õĸ������豸��" << std::endl;
    *starttime = -1;
}

/*int main() {
    DeviceManager manager;

    createDevices(manager);
    manager.printAllDevices();

    std::cout << "\n==== ģ������豸 ====" << std::endl;

    // ʾ�����Ե���
    callDeviceInterrupt(manager, 101, DeviceType::Disk, 3);
    callDeviceInterrupt(manager, 102, DeviceType::Disk, 4);
    callDeviceInterrupt(manager, 103, DeviceType::Disk, 5); // ����һ������
    callDeviceInterrupt(manager, 104, DeviceType::Disk, 2); // Ӧ��ʧ�ܣ��޿��У�

    std::this_thread::sleep_for(std::chrono::seconds(6)); // �ȴ������豸���
    std::cout << "\n==== �����豸״̬ ====" << std::endl;
    manager.printAllDevices();

    return 0;
}*/
