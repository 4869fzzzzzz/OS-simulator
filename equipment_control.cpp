#define  _CRT_SECURE_NO_WARNINGS 1
#include <iostream>
#include <vector>
#include <memory>
#include <string>

/// ===============================
/// ö�ٶ��壺�ж�����
/// ===============================
enum class InteruptType {
    DeviceReady,   // �豸׼����
    DeviceError,   // �豸��������
    IOCompleted,   // IO�������
    Custom         // �Զ����ж�����
};

/// ===============================
/// ģ���жϴ���������ϵͳ���ã�
/// ===============================
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

/// ===============================
/// ö�ٶ��壺�豸����
/// ===============================
enum class DeviceType {
    Disk,        // ����
    Printer,     // ��ӡ��
    Keyboard,    // ����
    NetworkCard, // ����
    Other        // �����豸
};

/// ===============================
/// �ඨ�壺�豸��
/// ===============================
class Device {
private:
    int id;                 // Ψһ�豸���
    DeviceType type;        // �豸����
    std::string name;       // �豸����
    bool enabled;           // �Ƿ�����
    bool hasInterrupt;      // �Ƿ����ж�

public:
    // ���캯��
    Device(int id, DeviceType type, const std::string& name)
        : id(id), type(type), name(name), enabled(false), hasInterrupt(false) {}

    // Getter ����
    int getId() const { return id; }
    DeviceType getType() const { return type; }
    std::string getName() const { return name; }
    bool isEnabled() const { return enabled; }
    bool isInterrupting() const { return hasInterrupt; }

    // �����豸����/����
    void enable() { enabled = true; }
    void disable() { enabled = false; }

    // �����жϱ�־
    void triggerInterrupt() { hasInterrupt = true; }
    void clearInterrupt() { hasInterrupt = false; }

    // �����жϣ����� raiseInterupt ������
    void generateInterupt(InteruptType type, int value) {
        hasInterrupt = true;
        raiseInterupt(type, id, value);
    }

    // ����豸��ǰ״̬
    void printStatus() const {
        std::cout << "[�豸] ID: " << id
            << ", ����: " << name
            << ", ����: " << static_cast<int>(type)
            << ", ״̬: " << (enabled ? "����" : "����")
            << ", �ж�: " << (hasInterrupt ? "��" : "��")
            << std::endl;
    }
};

/// ===============================
/// �ඨ�壺�豸��������
/// ===============================
class DeviceManager {
private:
    std::vector<std::shared_ptr<Device>> devices; // �����豸�б�
    int nextId = 1; // �Զ��������һ���豸ID

public:
    // �������豸������ָ��
    std::shared_ptr<Device> createDevice(DeviceType type, const std::string& name) {
        auto device = std::make_shared<Device>(nextId++, type, name);
        devices.push_back(device);
        return device;
    }

    // ����IDɾ���豸
    bool removeDevice(int id) {
        for (auto it = devices.begin(); it != devices.end(); ++it) {
            if ((*it)->getId() == id) {
                devices.erase(it);
                return true;
            }
        }
        return false;
    }

    // ����ID��ȡ�豸����
    std::shared_ptr<Device> getDevice(int id) {
        for (auto& device : devices) {
            if (device->getId() == id) return device;
        }
        return nullptr;
    }

    // ����ָ���豸���ж�
    void triggerDeviceInterupt(int id, InteruptType type, int value) {
        auto device = getDevice(id);
        if (device) {
            device->generateInterupt(type, value);
        }
        else {
            std::cerr << "[����] �Ҳ����豸 ID: " << id << std::endl;
        }
    }

    // ��ӡ�����豸״̬
    void printAllDevices() const {
        for (const auto& device : devices) {
            device->printStatus();
        }
    }
};

/// ===============================
/// �����򣺲����豸������
/// ===============================
int main() {
    DeviceManager manager;

    // ���������豸
    auto printer = manager.createDevice(DeviceType::Printer, "��ӡ��1");
    auto disk = manager.createDevice(DeviceType::Disk, "����A");

    // �����豸
    printer->enable();
    disk->enable();

    // ��ӡ��ǰ�豸״̬
    manager.printAllDevices();

    // ģ���豸�ж�
    manager.triggerDeviceInterupt(printer->getId(), InteruptType::DeviceReady, 0);
    manager.triggerDeviceInterupt(disk->getId(), InteruptType::IOCompleted, 128);

    // ��������豸���жϱ�־
    disk->clearInterrupt();

    std::cout << "\n���º�״̬��\n";
    manager.printAllDevices();

    return 0;
}
