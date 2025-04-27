#pragma once
#include "./headfile.h"
#include "./memory.h"

using namespace std;

// 文件权限定义
#define READ "r"      // 只读权限
#define WRITE "w"     // 写权限
#define EXECUTE "x"   // 执行权限

// 文件类型定义
#define FILE_TYPE 0      // 普通文件
#define DIRECTORY_TYPE 1 // 目录

//FCB
struct FCB {
    string filename;        // 文件名
    int fileType;           // 文件类型（普通文件/目录）
    int size;               // 文件大小
    p_address diskAddress;  // 存储该文件的磁盘地址
    time_t creationTime;    // 文件创建时间
    time_t lastModifiedTime;// 文件最后修改时间
    string permissions;     // 文件权限（rwx）
};

// 目录项  存储目录中的文件和子目录
struct DirEntry {
    string name; // 文件或目录名称
    FCB* fcb;   // 指向文件控制块
};

// 目录结构  用于组织文件和子目录的层次结构
struct Directory {
    string name;               // 目录名称
    vector<DirEntry> entries;  // 目录下的所有文件和子目录
    Directory* parent;         // 指向父目录
};

// 文件系统类
class FileSystem {
private:
    Directory* rootDir;                     // 根目录
    unordered_map<string, FCB*> fileTable;  // 文件路径到FCB的映射（加速查找）
    int totalBlocks;                        // 文件系统总块数
    int blockSize;                          // 每个块的大小
    vector<bool> diskUsage;                 // 记录磁盘块的使用情况

    // 分配磁盘空间
    p_address allocateDiskSpace(int size);
    // 释放磁盘空间
    void freeDiskSpace(p_address address, int size);
    Directory* findDirectory(const string& path);
    std::string format_time(time_t rawtime);
public:
    FileSystem(int totalBlocks, int blockSize); // 构造函数
    ~FileSystem();

    // 文件管理功能
    int createFile(string path, string filename, int fileType, int size); // 创建文件
    int deleteFile(string path, string filename); // 删除文件

    // 目录管理功能
    int createDirectory(const string& path, const string& dirname);
    int deleteDirectory(const string& path);
    vector<string> listDirectory(const string& path);
    int deleteDirectoryRecursive(const string& path);

    // 文件读写操作
    string readFile(string path, string filename); // 读取文件内容
    int writeFile(string path, string filename, string data); // 写入文件

    // 文件权限管理
    int setPermissions(string path, string filename, string permissions); // 设置文件权限

    // 文件系统信息
    void printFileSystemStructure(); // 打印文件系统的层次结构
    void printDirectory(const string& path);
    void printFreeSpaceList(); // 打印空闲空间信息
    void printUtilizationRate(); // 打印外存使用率
};