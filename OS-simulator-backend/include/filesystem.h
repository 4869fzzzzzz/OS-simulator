#pragma once
#include "./headfile.h"
#include "./memory.h"
#include <functional>

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
public:
    FileSystem(int totalBlocks, int blockSize); // 构造函数
    ~FileSystem();
    int getTotalBlocks() const { return totalBlocks; }
    int getBlockSize() const { return blockSize; }

    const vector<bool>& getDiskUsage() const;
    const Directory* getRootDirectory() const;
    string format_time(time_t rawtime) const;

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

    std::vector<std::string> getAllFilePaths(const std::string& directoryPath) {
        std::vector<std::string> filePaths;
        
        // 首先找到对应的目录
        Directory* dir = findDirectory(directoryPath);
        if (!dir) {
            return filePaths; // 目录不存在则返回空vector
        }
        
        // 遍历目录中的所有条目
        for (const DirEntry& entry : dir->entries) {
            if (entry.fcb->fileType == FILE_TYPE) {
                // 构建完整路径
                std::string fullPath = directoryPath;
                if (fullPath.back() != '/') fullPath += '/';
                fullPath += entry.name;
                
                filePaths.push_back(fullPath);
            }
        }
        
        return filePaths;
    }
};

extern FileSystem fs;

//总容量、已用空间、可用空间、总块数、块大小
class FilesystemOverview {
public:
    size_t total_capacity;      // 总容量（字节）
    size_t used_space;          // 已使用空间（字节）
    size_t free_space;          // 可用空间（字节）
    int total_blocks;           // 总块数
    int block_size;             // 块大小（字节）

    AIGC_JSON_HELPER(total_capacity, used_space, free_space, total_blocks, block_size)
};

void fillFilesystemOverview(FilesystemOverview& overview, const FileSystem& fs);

class DirectoryEntryForUI {
public:
    std::string name;            // 名称
    std::string type;            // 类型："目录" 或 "文件"
    std::string path;            // 路径
    std::vector<DirectoryEntryForUI> children;  // 子项

    AIGC_JSON_HELPER(name, type, path, children)
};

class FilesystemStructureForUI {
public:
    DirectoryEntryForUI root;

    AIGC_JSON_HELPER(root)
};

DirectoryEntryForUI buildDirectoryTree(const Directory* dir, const std::string& currentPath);
void fillFilesystemStructure(FilesystemStructureForUI& structure, const FileSystem& fs);

//文件名-类型-大小-创建时间-修改时间-权限
class FileInfoItemForUI {
public:
    std::string filename;         // 文件名
    std::string fileType;         // 类型："普通文件"/"目录"
    std::string size;             // 大小（带单位）
    std::string creationTime;     // 创建时间
    std::string lastModifiedTime; // 最后修改时间
    std::string permissions;      // 权限字符串（如 rwx）

    AIGC_JSON_HELPER(filename, fileType, size, creationTime, lastModifiedTime, permissions)
};

class FilesystemFileInfoTableForUI {
public:
    std::vector<FileInfoItemForUI> files;

    AIGC_JSON_HELPER(files)
};

void fillFilesystemFileInfoTable(FilesystemFileInfoTableForUI& table, const FileSystem& fs);

class FilesystemStatusForUI {
public:
    FilesystemOverview overview;
    FilesystemStructureForUI structure;
    FilesystemFileInfoTableForUI fileInfo;

    AIGC_JSON_HELPER(overview, structure, fileInfo)
};

void fillFilesystemStatus(FilesystemStatusForUI& status, const FileSystem& fs);