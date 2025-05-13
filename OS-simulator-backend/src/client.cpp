#include "../include/client.h"

void snapshotSend(int v1, int v2, std::string v3, int* v4, int v5) {
    try {
        // 创建系统快照对象
        SystemSnapshot snapshot;
        
        // 更新时钟数据
        snapshot.timer.startSysTime = timeToChar(get_startSysTime());
        snapshot.timer.nowSysTime = timeToChar(get_nowSysTime());
        snapshot.timer.time_cnt = time_cnt.load();
        
        // 更新进程状态
        static ProcessStatusManager processStatus;
        processStatus.update();
        snapshot.process = processStatus.getCurrentStatus();
        
        // 更新中断状态
        snapshot.interrupt.update();
        
        // 转换为JSON字符串
        std::string jsonStr;
        if(JsonHelper::ObjectToJson(snapshot, jsonStr)) {
            int result = send(clientSocket, jsonStr.c_str(), jsonStr.length(), 0);
            if (result == SOCKET_ERROR) {
                std::cerr << "[ERROR] Send failed with error: " << WSAGetLastError() << std::endl;
            } else {
                std::cout << "[INFO] System snapshot sent successfully (" << result << " bytes)" << std::endl;
            }
        } else {
            std::cerr << "[ERROR] Failed to convert snapshot to JSON" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Snapshot send error: " << e.what() << std::endl;
    }
}