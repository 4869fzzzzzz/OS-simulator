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
    std::cout << "[�ж�] ����: ";
    switch (t) {
    case InteruptType::DeviceReady: std::cout << "DeviceReady"; break;
    case InteruptType::DeviceError: std::cout << "DeviceError"; break;
    case InteruptType::IOCompleted: std::cout << "IOCompleted"; break;
    case InteruptType::Custom:      std::cout << "Custom"; break;
    }
    std::cout << ", �豸ID: " << device_id << ", ֵ: " << value << std::endl;
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
        std::cout << "[�豸] ID: " << id
            << ", ����: " << name
            << ", ����: " << static_cast<int>(type)
            << ", ״̬: " << (enabled ? "����" : "����")
            << ", �ж�: " << (hasInterrupt ? "��" : "��")
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

// ���������豸
void createDevices(DeviceManager& manager) {
    manager.createDevice(DeviceType::Disk, "����1");
    manager.createDevice(DeviceType::Disk, "����2");
    manager.createDevice(DeviceType::Disk, "����3");
    manager.createDevice(DeviceType::Printer, "��ӡ��1");
    manager.createDevice(DeviceType::Printer, "��ӡ��2");
    manager.createDevice(DeviceType::Keyboard, "����1");
    manager.createDevice(DeviceType::NetworkCard, "����1");
    manager.createDevice(DeviceType::Other, "�����豸1");
}

// �����ж������豸
bool callDeviceInterrupt(DeviceManager& manager, int pcb_id, DeviceType type, int seconds) {
    auto device = manager.findAvailableDevice(type);
    if (device) {
        device->enable();
        std::cout << "PCB " << pcb_id << " �����豸: " << device->getName()
            << "��������...��" << seconds << "�룩" << std::endl;

        std::thread([device, seconds]() {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            device->generateInterupt(InteruptType::IOCompleted, seconds);
            device->disable();
            device->clearInterrupt();
            }).detach();

            std::cout << "���أ�true���豸ID = " << device->getId() << std::endl;
            return true;
    }
    std::cout << "���أ�false��wrong��û�п��õĸ������豸��" << std::endl;
    return false;
}

int main() {
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
}
