#define  _CRT_SECURE_NO_WARNINGS 1
#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <thread>   // std::this_thread::sleep_for
#include <chrono>   // std::chrono::seconds

enum class InteruptType {
    DeviceReady,
    DeviceError,
    IOCompleted,
    Custom
};

void raiseInterupt(InteruptType t, int device_id, int value) {
    std::cout << "[中断] 类型: ";
    switch (t) {
    case InteruptType::DeviceReady: std::cout << "DeviceReady"; break;
    case InteruptType::DeviceError: std::cout << "DeviceError"; break;
    case InteruptType::IOCompleted: std::cout << "IOCompleted"; break;
    case InteruptType::Custom:      std::cout << "Custom"; break;
    }
    std::cout << ", 设备ID: " << device_id << ", 值: " << value << std::endl;
}

enum class DeviceType {
    Disk,
    Printer,
    Keyboard,
    NetworkCard,
    Other
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

    void generateInterupt(InteruptType type, int value) {
        hasInterrupt = true;
        raiseInterupt(type, id, value);
    }

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

// 创建所有设备
void createDevices(DeviceManager& manager) {
    manager.createDevice(DeviceType::Disk, "磁盘1");
    manager.createDevice(DeviceType::Disk, "磁盘2");
    manager.createDevice(DeviceType::Disk, "磁盘3");
    manager.createDevice(DeviceType::Printer, "打印机1");
    manager.createDevice(DeviceType::Printer, "打印机2");
    manager.createDevice(DeviceType::Keyboard, "键盘1");
    manager.createDevice(DeviceType::NetworkCard, "网卡1");
    manager.createDevice(DeviceType::Other, "其他设备1");
}

// 调用中断请求设备
bool callDeviceInterrupt(DeviceManager& manager, int pcb_id, DeviceType type, int seconds) {
    auto device = manager.findAvailableDevice(type);
    if (device) {
        device->enable();
        std::cout << "PCB " << pcb_id << " 请求设备: " << device->getName()
            << "，启用中...（" << seconds << "秒）" << std::endl;

        std::thread([device, seconds]() {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            device->generateInterupt(InteruptType::IOCompleted, seconds);
            device->disable();
            device->clearInterrupt();
            }).detach();

            std::cout << "返回：true，设备ID = " << device->getId() << std::endl;
            return true;
    }
    std::cout << "返回：false，wrong（没有可用的该类型设备）" << std::endl;
    return false;
}

int main() {
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
}
