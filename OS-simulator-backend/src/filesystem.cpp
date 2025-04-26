#include "../include/filesystem.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

FileSystem::FileSystem(int totalBlocks, int blockSize)
    : totalBlocks(totalBlocks), blockSize(blockSize) {
    rootDir = new Directory{ "/", {}, nullptr };
    diskUsage.resize(totalBlocks, false);
    memset(disk, 0, DISK_SIZE);
}

// 2. 析构函数实现
FileSystem::~FileSystem() {
    // 定义递归删除函数
    auto deleteDirectory = [](Directory* dir) -> void {
        // 先保存子目录列表，防止迭代器失效
        vector<Directory*> subdirs;

        // 第一遍遍历：处理文件和非递归目录
        for (auto& entry : dir->entries) {
            if (entry.fcb->fileType == FILE_TYPE) {
                delete entry.fcb;  // 直接删除文件
            }
            else {
                subdirs.push_back(reinterpret_cast<Directory*>(entry.fcb));
            }
        }

        // 第二遍遍历：递归删除子目录
        for (auto subdir : subdirs) {
            // 创建临时Lambda进行递归调用
            auto recursiveDelete = [](Directory* d) {
                // 使用函数指针解决递归类型推导问题
                static void (*del)(Directory*) = [](Directory* dir) {
                    for (auto& entry : dir->entries) {
                        if (entry.fcb->fileType == DIRECTORY_TYPE) {
                            del(reinterpret_cast<Directory*>(entry.fcb));
                        }
                        delete entry.fcb;
                    }
                    delete dir;
                };
                del(d);
            };
            recursiveDelete(subdir);
        }

        delete dir;  // 删除当前目录
    };

    deleteDirectory(rootDir);
}

Directory* FileSystem::findDirectory(const string& path) {
    if (path.empty() || path == "/") return rootDir;

    vector<string> components;
    stringstream ss(path);
    string item;

    // 拆分路径组件
    while (getline(ss, item, '/')) {
        if (!item.empty()) components.push_back(item);
    }

    Directory* current = rootDir;
    for (const auto& comp : components) {
        bool found = false;
        for (auto& entry : current->entries) {
            // 增加类型检查
            if (entry.name == comp &&
                entry.fcb->fileType == DIRECTORY_TYPE &&
                entry.fcb->diskAddress != FULL)
            {
                Directory* dir = reinterpret_cast<Directory*>(entry.fcb->diskAddress);
                // 内存有效性验证
                if (dir->parent == current) {  // 检查父指针一致性
                    current = dir;
                    found = true;
                    break;
                }
            }
        }
        if (!found) return nullptr;
    }
    return current;
}

int FileSystem::createFile(string path, string filename, int fileType, int size) {
    Directory* dir = findDirectory(path);
    if (!dir) {
        cerr << "创建文件失败: 目录 [" << path << "] 不存在\n";
        return -1;
    }

    // 检查文件名冲突
    for (const auto& entry : dir->entries) {
        if (entry.name == filename) {
            cerr << "创建文件失败: [" << filename << "] 已存在于目录 [" << path << "]\n";
            return -1;
        }
    }

    // 使用内存管理模块分配虚拟内存
    v_address v_addr;
    if (alloc_for_file(size, &v_addr) != 0) {
        cerr << "虚拟内存分配失败！\n";
        return -1;
    }

    // 创建FCB
    FCB* fcb = new FCB{
        filename,
        fileType,
        size,
        v_addr,  // 存储虚拟地址
        time(nullptr),
        time(nullptr),
        "rw-"  // 默认文件权限
    };

    // 更新目录项
    dir->entries.push_back({ filename, fcb });
    string fullpath = (path == "/") ? path + filename : path + "/" + filename;
    fileTable[fullpath] = fcb;

    cout << "创建文件成功 " << fullpath
        << " (大小: " << size << " bytes, 虚拟地址: "
        << v_addr << ")\n";
    return 0;
}

int FileSystem::deleteFile(string path, string filename) {
    Directory* dir = findDirectory(path);
    if (!dir) {
        cerr << "Directory not found: " << path << endl;
        return -1;
    }

    string fullpath = (path == "/") ? path + filename : path + "/" + filename;
    auto it = fileTable.find(fullpath);
    if (it == fileTable.end()) {
        cerr << "File not found: " << fullpath << endl;
        return -1;
    }

    FCB* fcb = it->second;
    freeDiskSpace(fcb->diskAddress, fcb->size);

    dir->entries.erase(remove_if(dir->entries.begin(), dir->entries.end(),
        [&](const DirEntry& e) { return e.name == filename; }),
        dir->entries.end());

    delete fcb;
    fileTable.erase(it);
    cout << "File " << fullpath << " deleted successfully" << endl;
    return 0;
}

string FileSystem::readFile(string path, string filename) {
    string fullpath = (path == "/") ? path + filename : path + "/" + filename;
    auto it = fileTable.find(fullpath);
    if (it == fileTable.end()) {
        cerr << "File not found: " << fullpath << endl;
        return "";
    }

    FCB* fcb = it->second;
    string content;
    content.resize(fcb->size);
    
    // 使用内存管理模块读取数据
    for (size_t i = 0; i < fcb->size; ++i) {
        atom_data data;
        v_address addr = fcb->diskAddress + i;  // 直接使用虚拟地址
        if (read_memory(&data, addr, FULL) != 0) {
            cerr << "Failed to read memory for file: " << fullpath << endl;
            return "";
        }
        content[i] = static_cast<char>(data);
    }
    
    cout << "[DEBUG] Read " << fcb->size << " bytes from file " << fullpath << endl;
    return content;
}

int FileSystem::writeFile(string path, string filename, string data) {
    string fullpath = (path == "/") ? path + filename : path + "/" + filename;
    auto it = fileTable.find(fullpath);
    if (it == fileTable.end()) {
        cerr << "File not found: " << fullpath << endl;
        return -1;
    }

    FCB* fcb = it->second;
    if (data.size() > fcb->size) {
        cerr << "Data size exceeds file size: " << data.size() << " > " << fcb->size << endl;
        return -1;
    }

    // 逐字节写入数据
    for (size_t i = 0; i < data.size(); ++i) {
        v_address addr = fcb->diskAddress + i;  // 直接使用虚拟地址
        if (write_memory(static_cast<atom_data>(data[i]), addr, FULL) != 0) {
            cerr << "Failed to write memory for file: " << fullpath << endl;
            return -1;
        }
    }
    
    fcb->lastModifiedTime = time(nullptr);
    cout << "[DEBUG] Writing data to memory at address: " << fcb->diskAddress << " Size: " << fcb->size << endl;
    return 0;
}

void FileSystem::printFreeSpaceList() {
    cout << "Free Blocks:\n";
    for (int i = 0; i < totalBlocks; ++i) {
        if (!diskUsage[i]) cout << i << " ";
    }
    cout << endl;
}

// 定义 allocateDiskSpace 函数
p_address FileSystem::allocateDiskSpace(int size) {
    int blocksNeeded = (size + blockSize - 1) / blockSize; // 向上取整
    int startBlock = -1;
    int freeBlocks = 0;

    for (int i = 0; i < totalBlocks; ++i) {
        if (!diskUsage[i]) {
            if (startBlock == -1) startBlock = i;
            ++freeBlocks;
            if (freeBlocks == blocksNeeded) {
                for (int j = startBlock; j < startBlock + blocksNeeded; ++j) {
                    diskUsage[j] = true;
                }
                cout << "Allocated " << blocksNeeded << " blocks starting from block " << startBlock << endl;
                return startBlock;
            }
        }
        else {
            startBlock = -1;
            freeBlocks = 0;
        }
    }
    cout << "No sufficient disk space found\n";
    return FULL; // 没有足够的连续空闲块
}



// 定义 freeDiskSpace 函数
void FileSystem::freeDiskSpace(p_address addr, int size) {
    int blocksToFree = (size + blockSize - 1) / blockSize; // 向上取整
    for (int i = addr; i < addr + blocksToFree; ++i) {
        diskUsage[i] = false;
    }
}

// 创建目录
int FileSystem::createDirectory(const string& path, const string& dirname) {
    Directory* parent = findDirectory(path);
    if (!parent) {
        cerr << "Create directory failed: Parent directory [" << path << "] not found\n";
        return -1;
    }

    // 检查同名目录是否存在
    for (const auto& entry : parent->entries) {
        if (entry.name == dirname) {
            cerr << "Create directory failed: [" << dirname << "] already exists in ["
                << path << "]\n";
            return -1;
        }
    }

    // 构建完整路径
    string fullpath = (path == "/") ? path + dirname : path + "/" + dirname;

    // 创建目录FCB
    FCB* dir_fcb = new FCB{
        dirname,
        DIRECTORY_TYPE,
        0,                   // 目录大小为0
        FULL,                // 不占用磁盘块
        time(nullptr),
        time(nullptr),
        "rwx"                // 默认权限
    };

    // 创建目录对象并关联
    Directory* new_dir = new Directory{
        dirname,
        vector<DirEntry>(),  // 明确初始化空vector
        parent
    };
    // 验证指针转换
    static_assert(sizeof(p_address) >= sizeof(Directory*), "p_address类型大小不足以存储指针");
    dir_fcb->diskAddress = reinterpret_cast<p_address>(new_dir);

    // 调试输出
    cout << "[DEBUG] 新建目录地址: " << new_dir
        << " 转换后: " << dir_fcb->diskAddress << endl;

    // 更新目录结构
    parent->entries.push_back({ dirname, dir_fcb });
    fileTable[fullpath] = dir_fcb;

    cout << "[DEBUG] 注册目录路径: " << fullpath
        << ", FCB地址: " << dir_fcb << endl;

    cout << "创建目录成功: " << fullpath
        << " (时间: " << format_time(dir_fcb->creationTime) << ")\n";
    return 0;
}

// 删除目录
int FileSystem::deleteDirectory(const string& path) {
    Directory* dir = findDirectory(path);
    if (!dir) {
        cerr << "Directory not found: " << path << endl;
        return -1;
    }

    // 检查是否为空目录
    if (!dir->entries.empty()) {
        cerr << "Cannot delete non-empty directory: " << path << endl;
        return -1;
    }

    // 从父目录删除条目
    Directory* parent = dir->parent;
    if (parent) {
        auto& entries = parent->entries;
        entries.erase(
            remove_if(entries.begin(), entries.end(),
                [&](const DirEntry& e) {
                    return e.fcb == fileTable[path];
                }),
            entries.end()
                    );
    }

    // 清理资源
    FCB* fcb = fileTable[path];
    delete dir;
    delete fcb;
    fileTable.erase(path);

    cout << "Deleted directory: " << path << endl;
    return 0;
}

// 递归删除目录
int FileSystem::deleteDirectoryRecursive(const string& path) {
    cout << "\n开始递归删除: " << path << endl;

    Directory* dir = findDirectory(path);
    if (!dir) {
        cerr << "删除失败: 目录 [" << path << "] 不存在\n";
        return -1;
    }

    // 删除所有子项
    vector<DirEntry> entries_copy = dir->entries;  // 防止迭代器失效
    for (const auto& entry : entries_copy) {
        string subpath = path + (path == "/" ? "" : "/") + entry.name;

        if (entry.fcb->fileType == DIRECTORY_TYPE) {
            cout << "│  ├─ 进入子目录: " << subpath << endl;
            if (deleteDirectoryRecursive(subpath) != 0) {
                cerr << "│  └─ 删除子目录失败: " << subpath << endl;
                return -1;
            }
        }
        else {
            cout << "│  ├─ 删除文件: " << entry.name
                << " (大小: " << entry.fcb->size << " bytes)" << endl;
            if (deleteFile(path, entry.name) != 0) {
                cerr << "│  └─ 文件删除失败: " << entry.name << endl;
                return -1;
            }
        }
    }

    // 删除当前目录
    cout << "└─ 删除目录: " << path << endl;
    if (deleteDirectory(path) != 0) {
        cerr << "   目录删除失败: " << path << endl;
        return -1;
    }

    return 0;
}

// 列出目录内容
vector<string> FileSystem::listDirectory(const string& path) {
    Directory* dir = findDirectory(path);
    vector<string> result;

    if (dir) {
        for (const auto& entry : dir->entries) {
            result.push_back(entry.name + (entry.fcb->fileType == DIRECTORY_TYPE ? "/" : ""));
        }
    }

    return result;
}

// 打印目录结构
void FileSystem::printDirectory(const string& path) {
    Directory* dir = findDirectory(path);
    if (!dir) {
        cerr << "目录不存在: " << path << endl;
        return;
    }

    // 检查目录FCB有效性
    FCB* dir_fcb = fileTable[path];
    if (!dir_fcb || dir_fcb->fileType != DIRECTORY_TYPE) {
        cerr << "非法目录元数据: " << path << endl;
        return;
    }

    // 打印有效信息
    cout << "\n目录内容: " << path;
    cout << "\n┌───────────────────────────────────────────────┐";
    cout << "\n│ 最后修改时间: " << format_time(dir_fcb->lastModifiedTime);
    cout << "\n├───────────────┬─────────────┬──────────────────┤";
    cout << "\n│     类型      │    名称     │      大小        │";
    cout << "\n├───────────────┼─────────────┼──────────────────┤";

    if (dir->entries.empty()) {
        cout << "\n│              空目录                          │";
    }
    else {
        for (const auto& entry : dir->entries) {
            // 增加条目有效性检查
            if (!entry.fcb) {
                cerr << "\n!! 发现无效目录条目 !!\n";
                continue;
            }

            string type = (entry.fcb->fileType == DIRECTORY_TYPE) ? "目录" : "文件";
            string size_str = (entry.fcb->fileType == DIRECTORY_TYPE) ?
                "-" :
                to_string(entry.fcb->size) + " bytes";

            cout << "\n│ " << left << setw(13) << type
                << "│ " << setw(11) << entry.name
                << "│ " << setw(16) << size_str << "│";
        }
    }
    cout << "\n└───────────────┴─────────────┴──────────────────┘\n";
}

//辅助函数
string FileSystem::format_time(time_t rawtime) {
    struct tm* timeinfo = localtime(&rawtime);
    char buffer[80];
    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", timeinfo);
    return string(buffer);
}

