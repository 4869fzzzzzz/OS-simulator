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

// 2. ��������ʵ��
FileSystem::~FileSystem() {
    // ����ݹ�ɾ������
    auto deleteDirectory = [](Directory* dir) -> void {
        // �ȱ�����Ŀ¼�б���ֹ������ʧЧ
        vector<Directory*> subdirs;

        // ��һ������������ļ��ͷǵݹ�Ŀ¼
        for (auto& entry : dir->entries) {
            if (entry.fcb->fileType == FILE_TYPE) {
                delete entry.fcb;  // ֱ��ɾ���ļ�
            }
            else {
                subdirs.push_back(reinterpret_cast<Directory*>(entry.fcb));
            }
        }

        // �ڶ���������ݹ�ɾ����Ŀ¼
        for (auto subdir : subdirs) {
            // ������ʱLambda���еݹ����
            auto recursiveDelete = [](Directory* d) {
                // ʹ�ú���ָ�����ݹ������Ƶ�����
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

        delete dir;  // ɾ����ǰĿ¼
    };

    deleteDirectory(rootDir);
}

Directory* FileSystem::findDirectory(const string& path) {
    if (path.empty() || path == "/") return rootDir;

    vector<string> components;
    stringstream ss(path);
    string item;

    // ���·�����
    while (getline(ss, item, '/')) {
        if (!item.empty()) components.push_back(item);
    }

    Directory* current = rootDir;
    for (const auto& comp : components) {
        bool found = false;
        for (auto& entry : current->entries) {
            // �������ͼ��
            if (entry.name == comp &&
                entry.fcb->fileType == DIRECTORY_TYPE &&
                entry.fcb->diskAddress != FULL)
            {
                Directory* dir = reinterpret_cast<Directory*>(entry.fcb->diskAddress);
                // �ڴ���Ч����֤
                if (dir->parent == current) {  // ��鸸ָ��һ����
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
        cerr << "�����ļ�ʧ��: Ŀ¼ [" << path << "] ������\n";
        return -1;
    }

    // ����ļ�����ͻ
    for (const auto& entry : dir->entries) {
        if (entry.name == filename) {
            cerr << "�����ļ�ʧ��: [" << filename << "] �Ѵ�����Ŀ¼ [" << path << "]\n";
            return -1;
        }
    }

    // ʹ���ڴ����ģ����������ڴ�
    v_address v_addr;
    if (alloc_for_file(size, &v_addr) != 0) {
        cerr << "�����ڴ����ʧ�ܣ�\n";
        return -1;
    }

    // ����FCB
    FCB* fcb = new FCB{
        filename,
        fileType,
        size,
        v_addr,  // �洢�����ַ
        time(nullptr),
        time(nullptr),
        "rw-"  // Ĭ���ļ�Ȩ��
    };

    // ����Ŀ¼��
    dir->entries.push_back({ filename, fcb });
    string fullpath = (path == "/") ? path + filename : path + "/" + filename;
    fileTable[fullpath] = fcb;

    cout << "�����ļ��ɹ� " << fullpath
        << " (��С: " << size << " bytes, �����ַ: "
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
    
    // ʹ���ڴ����ģ���ȡ����
    for (size_t i = 0; i < fcb->size; ++i) {
        atom_data data;
        v_address addr = fcb->diskAddress + i;  // ֱ��ʹ�������ַ
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

    // ���ֽ�д������
    for (size_t i = 0; i < data.size(); ++i) {
        v_address addr = fcb->diskAddress + i;  // ֱ��ʹ�������ַ
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

// ���� allocateDiskSpace ����
p_address FileSystem::allocateDiskSpace(int size) {
    int blocksNeeded = (size + blockSize - 1) / blockSize; // ����ȡ��
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
    return FULL; // û���㹻���������п�
}



// ���� freeDiskSpace ����
void FileSystem::freeDiskSpace(p_address addr, int size) {
    int blocksToFree = (size + blockSize - 1) / blockSize; // ����ȡ��
    for (int i = addr; i < addr + blocksToFree; ++i) {
        diskUsage[i] = false;
    }
}

// ����Ŀ¼
int FileSystem::createDirectory(const string& path, const string& dirname) {
    Directory* parent = findDirectory(path);
    if (!parent) {
        cerr << "Create directory failed: Parent directory [" << path << "] not found\n";
        return -1;
    }

    // ���ͬ��Ŀ¼�Ƿ����
    for (const auto& entry : parent->entries) {
        if (entry.name == dirname) {
            cerr << "Create directory failed: [" << dirname << "] already exists in ["
                << path << "]\n";
            return -1;
        }
    }

    // ��������·��
    string fullpath = (path == "/") ? path + dirname : path + "/" + dirname;

    // ����Ŀ¼FCB
    FCB* dir_fcb = new FCB{
        dirname,
        DIRECTORY_TYPE,
        0,                   // Ŀ¼��СΪ0
        FULL,                // ��ռ�ô��̿�
        time(nullptr),
        time(nullptr),
        "rwx"                // Ĭ��Ȩ��
    };

    // ����Ŀ¼���󲢹���
    Directory* new_dir = new Directory{
        dirname,
        vector<DirEntry>(),  // ��ȷ��ʼ����vector
        parent
    };
    // ��ָ֤��ת��
    static_assert(sizeof(p_address) >= sizeof(Directory*), "p_address���ʹ�С�����Դ洢ָ��");
    dir_fcb->diskAddress = reinterpret_cast<p_address>(new_dir);

    // �������
    cout << "[DEBUG] �½�Ŀ¼��ַ: " << new_dir
        << " ת����: " << dir_fcb->diskAddress << endl;

    // ����Ŀ¼�ṹ
    parent->entries.push_back({ dirname, dir_fcb });
    fileTable[fullpath] = dir_fcb;

    cout << "[DEBUG] ע��Ŀ¼·��: " << fullpath
        << ", FCB��ַ: " << dir_fcb << endl;

    cout << "����Ŀ¼�ɹ�: " << fullpath
        << " (ʱ��: " << format_time(dir_fcb->creationTime) << ")\n";
    return 0;
}

// ɾ��Ŀ¼
int FileSystem::deleteDirectory(const string& path) {
    Directory* dir = findDirectory(path);
    if (!dir) {
        cerr << "Directory not found: " << path << endl;
        return -1;
    }

    // ����Ƿ�Ϊ��Ŀ¼
    if (!dir->entries.empty()) {
        cerr << "Cannot delete non-empty directory: " << path << endl;
        return -1;
    }

    // �Ӹ�Ŀ¼ɾ����Ŀ
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

    // ������Դ
    FCB* fcb = fileTable[path];
    delete dir;
    delete fcb;
    fileTable.erase(path);

    cout << "Deleted directory: " << path << endl;
    return 0;
}

// �ݹ�ɾ��Ŀ¼
int FileSystem::deleteDirectoryRecursive(const string& path) {
    cout << "\n��ʼ�ݹ�ɾ��: " << path << endl;

    Directory* dir = findDirectory(path);
    if (!dir) {
        cerr << "ɾ��ʧ��: Ŀ¼ [" << path << "] ������\n";
        return -1;
    }

    // ɾ����������
    vector<DirEntry> entries_copy = dir->entries;  // ��ֹ������ʧЧ
    for (const auto& entry : entries_copy) {
        string subpath = path + (path == "/" ? "" : "/") + entry.name;

        if (entry.fcb->fileType == DIRECTORY_TYPE) {
            cout << "��  ���� ������Ŀ¼: " << subpath << endl;
            if (deleteDirectoryRecursive(subpath) != 0) {
                cerr << "��  ���� ɾ����Ŀ¼ʧ��: " << subpath << endl;
                return -1;
            }
        }
        else {
            cout << "��  ���� ɾ���ļ�: " << entry.name
                << " (��С: " << entry.fcb->size << " bytes)" << endl;
            if (deleteFile(path, entry.name) != 0) {
                cerr << "��  ���� �ļ�ɾ��ʧ��: " << entry.name << endl;
                return -1;
            }
        }
    }

    // ɾ����ǰĿ¼
    cout << "���� ɾ��Ŀ¼: " << path << endl;
    if (deleteDirectory(path) != 0) {
        cerr << "   Ŀ¼ɾ��ʧ��: " << path << endl;
        return -1;
    }

    return 0;
}

// �г�Ŀ¼����
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

// ��ӡĿ¼�ṹ
void FileSystem::printDirectory(const string& path) {
    Directory* dir = findDirectory(path);
    if (!dir) {
        cerr << "Ŀ¼������: " << path << endl;
        return;
    }

    // ���Ŀ¼FCB��Ч��
    FCB* dir_fcb = fileTable[path];
    if (!dir_fcb || dir_fcb->fileType != DIRECTORY_TYPE) {
        cerr << "�Ƿ�Ŀ¼Ԫ����: " << path << endl;
        return;
    }

    // ��ӡ��Ч��Ϣ
    cout << "\nĿ¼����: " << path;
    cout << "\n��������������������������������������������������������������������������������������������������";
    cout << "\n�� ����޸�ʱ��: " << format_time(dir_fcb->lastModifiedTime);
    cout << "\n���������������������������������Щ��������������������������Щ�������������������������������������";
    cout << "\n��     ����      ��    ����     ��      ��С        ��";
    cout << "\n���������������������������������੤�������������������������੤������������������������������������";

    if (dir->entries.empty()) {
        cout << "\n��              ��Ŀ¼                          ��";
    }
    else {
        for (const auto& entry : dir->entries) {
            // ������Ŀ��Ч�Լ��
            if (!entry.fcb) {
                cerr << "\n!! ������ЧĿ¼��Ŀ !!\n";
                continue;
            }

            string type = (entry.fcb->fileType == DIRECTORY_TYPE) ? "Ŀ¼" : "�ļ�";
            string size_str = (entry.fcb->fileType == DIRECTORY_TYPE) ?
                "-" :
                to_string(entry.fcb->size) + " bytes";

            cout << "\n�� " << left << setw(13) << type
                << "�� " << setw(11) << entry.name
                << "�� " << setw(16) << size_str << "��";
        }
    }
    cout << "\n���������������������������������ة��������������������������ة�������������������������������������\n";
}

//��������
string FileSystem::format_time(time_t rawtime) {
    struct tm* timeinfo = localtime(&rawtime);
    char buffer[80];
    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", timeinfo);
    return string(buffer);
}

