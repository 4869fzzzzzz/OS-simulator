#pragma once
#include "./headfile.h"
#include "./interrupt.h"

enum class DeviceType {
    Disk=0,
    Printer=1,
    Keyboard=2,
    NetworkCard=3,
    Other=4
};

class Device {
private:
    int id;
    DeviceType type;
    std::string name;
    bool enabled;
    bool hasInterrupt;

public:
    Device(int id, DeviceType type, const std::string& name)
        : id(id), type(type), name(name), enabled(false), hasInterrupt(false) {}

    int getId() const { return id; }
    DeviceType getType() const { return type; }
    std::string getName() const { return name; }
    bool isEnabled() const { return enabled; }
    bool isInterrupting() const { return hasInterrupt; }

    void enable() { enabled = true; }
    void disable() { enabled = false; }
    void triggerInterrupt() { hasInterrupt = true; }
    void clearInterrupt() { hasInterrupt = false; }

    /*void generateInterupt(InteruptType type, int value) {
        hasInterrupt = true;
        raiseInterupt(type, id, value);
    }*/

    void printStatus() const {
        std::cout << "[设备] ID: " << id
            << ", 名称: " << name
            << ", 类型: " << static_cast<int>(type)
            << ", 状态: " << (enabled ? "启用" : "禁用")
            << ", 中断: " << (hasInterrupt ? "是" : "否")
            << std::endl;
    }
};

class DeviceManager {
private:
    std::vector<std::shared_ptr<Device>> devices;
    int nextId = 1;

public:
    std::shared_ptr<Device> createDevice(DeviceType type, const std::string& name) {
        auto device = std::make_shared<Device>(nextId++, type, name);
        devices.push_back(device);
        return device;
    }

    std::shared_ptr<Device> findAvailableDevice(DeviceType type) {
        for (auto& device : devices) {
            if (device->getType() == type && !device->isEnabled()) {
                return device;
            }
        }
        return nullptr;
    }

    void printAllDevices() const {
        for (const auto& device : devices) {
            device->printStatus();
        }
    }
};

void createDevices(DeviceManager& manager);
void callDeviceInterrupt(int pcb_id, int type, std::string info,int* flag, int seconds) ;


extern DeviceManager manager;