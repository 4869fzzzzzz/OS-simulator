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
    int using_pid{-1};
    size_t memory_address{0};
    std::atomic<long long> total_use_time{0};
    std::atomic<int> use_count{0};

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
     int getUsingPid() const { return using_pid; }
    size_t getMemoryAddress() const { return memory_address; }
    long long getTotalUseTime() const { return total_use_time.load(); }
    int getUseCount() const { return use_count.load(); }
    
    void setUsingPid(int pid) { 
        using_pid = pid; 
        if (pid != -1) {
            use_count.fetch_add(1);
        }
    }
    void setMemoryAddress(size_t addr) { memory_address = addr; }
    void addUseTime(long long time) { total_use_time.fetch_add(time); }

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
    const std::vector<std::shared_ptr<Device>>& getDevices() const {
        return devices;
    }
};

// 设备状态项
struct DeviceStatusItemForUI {
    int device_id;              // 设备ID
    std::string name;           // 设备名称
    std::string type;           // 设备类型
    bool is_occupied;           // 是否被占用
    int using_pid;             // 占用进程PID
    size_t memory_address;     // 内存起始地址
    
    AIGC_JSON_HELPER(device_id, name, type, is_occupied, using_pid, memory_address)
    AIGC_JSON_HELPER_RENAME("id", "name", "type", "occupied", "pid", "memory")
};

// 设备系统总览
struct DeviceOverviewForUI {
    int total_devices;         // 总设备数
    int used_devices;          // 使用中设备数
    int available_devices;     // 可用设备数
    long long total_use_time;  // 设备使用总时间
    int total_use_count;      // 设备使用总次数
    
    AIGC_JSON_HELPER(total_devices, used_devices, available_devices, total_use_time, total_use_count)
    AIGC_JSON_HELPER_RENAME("total", "used", "available", "useTime", "useCount")
};

// 完整的设备系统状态
struct DeviceSystemStatusForUI {
    DeviceOverviewForUI overview;
    std::vector<DeviceStatusItemForUI> device_table;
    
    AIGC_JSON_HELPER(overview, device_table)
    AIGC_JSON_HELPER_RENAME("overview", "devices")
};
// ...existing code...

// 设备状态管理类
class DeviceStatusManager {
private:
    DeviceSystemStatusForUI current_status;
    std::atomic<bool> need_update{true};

public:
    DeviceStatusManager() = default;
    
    void update() {
        if (!need_update) return;
        
        // 更新总览信息
        auto& overview = current_status.overview;
        overview.total_devices = 0;
        overview.used_devices = 0;
        overview.available_devices = 0;
        overview.total_use_time = 0;
        overview.total_use_count = 0;
        
        // 更新设备表
        current_status.device_table.clear();
        
        // 从全局manager获取设备信息
        for (const auto& device : manager.getDevices()) {
            DeviceStatusItemForUI item;
            item.device_id = device->getId();
            item.name = device->getName();
            item.type = getDeviceTypeString(device->getType());
            item.is_occupied = device->isEnabled();
            item.using_pid = device->getUsingPid();
            item.memory_address = device->getMemoryAddress();
            
            current_status.device_table.push_back(item);
            
            // 更新统计信息
            overview.total_devices++;
            if (device->isEnabled()) {
                overview.used_devices++;
            } else {
                overview.available_devices++;
            }
            overview.total_use_time += device->getTotalUseTime();
            overview.total_use_count += device->getUseCount();
        }
        
        need_update = false;
    }
    
    const DeviceSystemStatusForUI& getCurrentStatus() {
        update();
        return current_status;
    }
    
    void markForUpdate() {
        need_update = true;
    }
    
private:
    static std::string getDeviceTypeString(DeviceType type) {
        switch (type) {
            case DeviceType::Disk: return "磁盘";
            case DeviceType::Printer: return "打印机";
            case DeviceType::Keyboard: return "键盘";
            case DeviceType::NetworkCard: return "网卡";
            case DeviceType::Other: return "其他";
            default: return "未知";
        }
    }
};

// 在Device类中添加必要的接口

void Init_Device();
void createDevices(DeviceManager& manager);
void callDeviceInterrupt(int pcb_id, int type, std::string info,int* flag, int seconds) ;


extern DeviceManager manager;