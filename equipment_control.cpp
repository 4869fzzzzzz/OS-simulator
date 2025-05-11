#define  _CRT_SECURE_NO_WARNINGS 1
#include <iostream>
#include <vector>
#include <memory>
#include <string>

/// ===============================
/// 枚举定义：中断类型
/// ===============================
enum class InteruptType {
    DeviceReady,   // 设备准备好
    DeviceError,   // 设备发生错误
    IOCompleted,   // IO操作完成
    Custom         // 自定义中断类型
};

/// ===============================
/// 模拟中断处理函数（由系统调用）
/// ===============================
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

/// ===============================
/// 枚举定义：设备类型
/// ===============================
enum class DeviceType {
    Disk,        // 磁盘
    Printer,     // 打印机
    Keyboard,    // 键盘
    NetworkCard, // 网卡
    Other        // 其他设备
};

/// ===============================
/// 类定义：设备类
/// ===============================
class Device {
private:
    int id;                 // 唯一设备编号
    DeviceType type;        // 设备类型
    std::string name;       // 设备名称
    bool enabled;           // 是否启用
    bool hasInterrupt;      // 是否有中断

public:
    // 构造函数
    Device(int id, DeviceType type, const std::string& name)
        : id(id), type(type), name(name), enabled(false), hasInterrupt(false) {}

    // Getter 方法
    int getId() const { return id; }
    DeviceType getType() const { return type; }
    std::string getName() const { return name; }
    bool isEnabled() const { return enabled; }
    bool isInterrupting() const { return hasInterrupt; }

    // 设置设备启用/禁用
    void enable() { enabled = true; }
    void disable() { enabled = false; }

    // 设置中断标志
    void triggerInterrupt() { hasInterrupt = true; }
    void clearInterrupt() { hasInterrupt = false; }

    // 生成中断（触发 raiseInterupt 函数）
    void generateInterupt(InteruptType type, int value) {
        hasInterrupt = true;
        raiseInterupt(type, id, value);
    }

    // 输出设备当前状态
    void printStatus() const {
        std::cout << "[设备] ID: " << id
            << ", 名称: " << name
            << ", 类型: " << static_cast<int>(type)
            << ", 状态: " << (enabled ? "启用" : "禁用")
            << ", 中断: " << (hasInterrupt ? "是" : "否")
            << std::endl;
    }
};

/// ===============================
/// 类定义：设备管理器类
/// ===============================
class DeviceManager {
private:
    std::vector<std::shared_ptr<Device>> devices; // 所有设备列表
    int nextId = 1; // 自动分配的下一个设备ID

public:
    // 创建新设备并返回指针
    std::shared_ptr<Device> createDevice(DeviceType type, const std::string& name) {
        auto device = std::make_shared<Device>(nextId++, type, name);
        devices.push_back(device);
        return device;
    }

    // 根据ID删除设备
    bool removeDevice(int id) {
        for (auto it = devices.begin(); it != devices.end(); ++it) {
            if ((*it)->getId() == id) {
                devices.erase(it);
                return true;
            }
        }
        return false;
    }

    // 根据ID获取设备对象
    std::shared_ptr<Device> getDevice(int id) {
        for (auto& device : devices) {
            if (device->getId() == id) return device;
        }
        return nullptr;
    }

    // 触发指定设备的中断
    void triggerDeviceInterupt(int id, InteruptType type, int value) {
        auto device = getDevice(id);
        if (device) {
            device->generateInterupt(type, value);
        }
        else {
            std::cerr << "[错误] 找不到设备 ID: " << id << std::endl;
        }
    }

    // 打印所有设备状态
    void printAllDevices() const {
        for (const auto& device : devices) {
            device->printStatus();
        }
    }
};

/// ===============================
/// 主程序：测试设备管理功能
/// ===============================
int main() {
    DeviceManager manager;

    // 创建两个设备
    auto printer = manager.createDevice(DeviceType::Printer, "打印机1");
    auto disk = manager.createDevice(DeviceType::Disk, "磁盘A");

    // 启用设备
    printer->enable();
    disk->enable();

    // 打印当前设备状态
    manager.printAllDevices();

    // 模拟设备中断
    manager.triggerDeviceInterupt(printer->getId(), InteruptType::DeviceReady, 0);
    manager.triggerDeviceInterupt(disk->getId(), InteruptType::IOCompleted, 128);

    // 清除磁盘设备的中断标志
    disk->clearInterrupt();

    std::cout << "\n更新后状态：\n";
    manager.printAllDevices();

    return 0;
}
