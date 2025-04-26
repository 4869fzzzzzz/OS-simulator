// memory.cpp
#include <iostream>
#include "../include/memory.h"
#include <Windows.h>
#include "../include/filesystem.h"
#include <vector>
#include <algorithm>
#include <WinNls.h>

using namespace std;

// ---------------- 全局变量定义 ----------------

// 页表数组
PageTableItem page_table[PAGE_TABLE_SIZE];
// 虚拟页使用情况位图
page_bit v_page[V_PAGE_USE_SIZE] = { 0 };
// 物理页使用情况位图
page_bit p_page[P_PAGE_USE_SIZE] = { 0 };
// Clock页面置换算法的指针（时钟指针）
Frame* clock_hand = nullptr;
// 主存（包括内存和交换区）
atom_data memory[MEMORY_SIZE + SWAP_SIZE] = { 0 };
// 磁盘空间（存放被换出的页面）
atom_data disk[DISK_SIZE] = { 0 };

// ---------------- 辅助函数声明 ----------------

// 查找一个空闲的物理页
static p_address find_free_ppage();
// 初始化物理内存帧链表
static void setup_frame_list();
// 更新时钟指针
static void update_clock_hand();
// 释放某进程所有占用的页面
static void free_pt_by_pid(m_pid pid);

// ---------------- 主功能函数实现 ----------------

// 初始化内存系统
void init_memory() {
    memset(page_table, 0, sizeof(page_table));
    memset(v_page, 0, sizeof(v_page));
    memset(p_page, 0, sizeof(p_page));
    setup_frame_list();
}

// 为指定进程分配一定数量的虚拟内存
v_address alloc_for_process(m_pid pid, m_size size) {
    int pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE; // 需要的页数
    int start_page = -1;
    int consecutive = 0;

    // 查找连续的空闲虚拟页
    for (int i = 0; i < V_PAGE_USE_SIZE; ++i) {
        if (!(v_page[i / 8] & (1 << (i % 8)))) { // 空闲页
            if (++consecutive == pages_needed) {
                start_page = i - pages_needed + 1;
                break;
            }
        } else {
            consecutive = 0;
        }
    }

    if (start_page == -1) return FULL; // 无法分配，返回FULL

    // 标记已分配的虚拟页，并设置页表项
    for (int i = 0; i < pages_needed; ++i) {
        int v_page_num = start_page + i;
        v_page[v_page_num / 8] |= 1 << (v_page_num % 8);

        PageTableItem& pt = page_table[v_page_num];
        pt.v_id = v_page_num;
        pt.p_id = find_free_ppage(); // 查找空闲物理页
        pt.owner = pid;
        pt.in_memory = (pt.p_id != FULL);
        pt.used = false;

        if (pt.p_id != FULL) {
            p_page[pt.p_id / 8] |= 1 << (pt.p_id % 8);
            memset(&memory[pt.p_id * PAGE_SIZE], 0, PAGE_SIZE);
        } else {
            // 没有物理页可用，需要换入页面
            page_in(v_page_num * PAGE_SIZE, pid);
        }
    }
    return start_page * PAGE_SIZE;
}

// 释放指定进程占用的所有虚拟内存
void free_process_memory(m_pid pid) {
    free_pt_by_pid(pid);
}

// 将虚拟页调入内存
int page_in(v_address v_addr, m_pid pid) {
    page v_page_num = v_addr / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];

    // 查找空闲物理页
    page p_page_num = find_free_ppage();
    if (p_page_num == FULL) {
        // 无空闲物理页时使用clock置换
        p_page_num = clock_replace();
        if (p_page_num == FULL) {
            cerr << "[ERROR] No available physical page for page in" << endl;
            return -1;
        }
        cout << "[DEBUG] Page replacement triggered: Victim page " << p_page_num << " (Virtual page " << v_page_num << ")" << endl;
    }

    // 更新页表项
    pt.p_id = p_page_num;
    pt.in_memory = true;
    p_page[p_page_num / 8] |= 1 << (p_page_num % 8);

    // 从磁盘中恢复页面内容到内存
    memcpy(&memory[p_page_num * PAGE_SIZE],
           &disk[v_page_num * PAGE_SIZE],
           PAGE_SIZE);
    cout << "[DEBUG] Page in: Virtual page " << v_page_num << " -> Physical page " << p_page_num << endl;
    return 0;
}

page clock_replace() {
    Frame* current = clock_hand;
    while (true) {
        if (!current->used) {
            // 找到一个未被使用的帧，进行置换
            page victim = current->p_id;
            
            // 查找对应的虚拟页
            for (int i = 0; i < PAGE_TABLE_SIZE; ++i) {
                if (page_table[i].p_id == victim && page_table[i].owner == current->owner) {
                    // 将内存数据写入磁盘
                    memcpy(&disk[page_table[i].v_id * PAGE_SIZE],
                           &memory[victim * PAGE_SIZE],
                           PAGE_SIZE);
                    
                    // 更新页表项
                    page_table[i].in_memory = false;
                    page_table[i].p_id = FULL;
                    p_page[victim / 8] &= ~(1 << (victim % 8));
                    
                    clock_hand = current->next; // 移动时钟指针
                    cout << "[DEBUG] Page out: Physical page " << victim << " -> Virtual page " << current->v_id << endl;
                    return victim;
                }
            }
            cerr << "[ERROR] Failed to find corresponding virtual page for physical page " << victim << endl;
            return FULL;
        }
        current->used = false;
        current = current->next;
    }
}

// ---------------- 辅助函数实现 ----------------

// 查找第一个空闲的物理页
p_address find_free_ppage() {
    for (int i = 0; i < P_PAGE_USE_SIZE; ++i) {
        if (!(p_page[i / 8] & (1 << (i % 8)))) return i;
    }
    return FULL;
}

// 初始化物理帧链表，为Clock置换算法做准备
void setup_frame_list() {
    Frame* prev = nullptr;
    for (int i = 0; i < P_PAGE_USE_SIZE; ++i) {
        Frame* frame = new Frame();
        frame->p_id = i;
        frame->v_id = FULL;
        frame->owner = FULL;
        frame->used = false;

        if (prev) prev->next = frame;
        else clock_hand = frame;
        prev = frame;
    }
    prev->next = clock_hand; // 循环链表
}

// 释放指定进程的所有页表项及占用的物理页
void free_pt_by_pid(m_pid pid) {
    for (int i = 0; i < PAGE_TABLE_SIZE; ++i) {
        if (page_table[i].owner == pid) {
            page_table[i].owner = FULL;
            v_address v_page_num = page_table[i].v_id;
            v_page[v_page_num / 8] &= ~(1 << (v_page_num % 8));
            if (page_table[i].in_memory) {
                p_page[page_table[i].p_id / 8] &= ~(1 << (page_table[i].p_id % 8));
            }
            memset(&page_table[i], 0, sizeof(PageTableItem));
        }
    }
}

//从虚拟地址读取一个字节的数据
int read_memory(atom_data* data, v_address address, m_pid pid) {
    page v_page_num = address / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];
    if (!pt.in_memory) return -1; // 页不在内存中
    *data = memory[pt.p_id * PAGE_SIZE + (address % PAGE_SIZE)];
    pt.used = true; // 标记为访问过
    return 0;
}

// 将一个字节写入到指定的虚拟地址
int write_memory(atom_data data, v_address address, m_pid pid) {
    page v_page_num = address / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];
    if (!pt.in_memory) return -1; // 页不在内存中
    memory[pt.p_id * PAGE_SIZE + (address % PAGE_SIZE)] = data;
    pt.used = true; // 标记为访问过
    return 0;
}

// 为设备分配内存缓冲区（待定）
v_address alloc_for_device(int device_id, m_size size) {
    int pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    int start_page = -1;
    int consecutive = 0;

    // 在设备缓冲区范围内查找连续空闲页
    int buffer_start = DEVICE_BUFFER_START / PAGE_SIZE;
    int buffer_end = buffer_start + DEVICE_BUFFER_SIZE / PAGE_SIZE;

    for (int i = buffer_start; i < buffer_end; ++i) {
        if (!(v_page[i / 8] & (1 << (i % 8)))) {
            if (++consecutive == pages_needed) {
                start_page = i - pages_needed + 1;
                break;
            }
        } else {
            consecutive = 0;
        }
    }

    if (start_page == -1) return FULL;

    // 标记已分配的虚拟页
    for (int i = 0; i < pages_needed; ++i) {
        int v_page_num = start_page + i;
        v_page[v_page_num / 8] |= 1 << (v_page_num % 8);

        PageTableItem& pt = page_table[v_page_num];
        pt.v_id = v_page_num;
        pt.p_id = find_free_ppage();
        pt.owner = FULL; // 设备内存不属于任何进程
        pt.in_memory = (pt.p_id != FULL);
        pt.used = false;

        if (pt.p_id != FULL) {
            p_page[pt.p_id / 8] |= 1 << (pt.p_id % 8);
            memset(&memory[pt.p_id * PAGE_SIZE], 0, PAGE_SIZE);
        }
    }
    return start_page * PAGE_SIZE;
}

//为文件分配内存
int alloc_for_file(m_size size, v_address* addr) {
    v_address allocated = alloc_for_process(FULL, size);
    if (allocated == FULL) return -1;
    *addr = allocated;
    return 0;
}

//释放文件占用的内存
void free_file_memory(v_address addr) {
    page v_page_num = addr / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];
    
    if (pt.owner == FULL) { // 确保是文件内存
        v_page[v_page_num / 8] &= ~(1 << (v_page_num % 8));
        if (pt.in_memory) {
            p_page[pt.p_id / 8] &= ~(1 << (pt.p_id % 8));
        }
        memset(&pt, 0, sizeof(PageTableItem));
    }
}

// 将物理内存页面换出到磁盘(无用待删)
int page_out(p_address p_addr, m_pid pid) {
    // 查找对应的虚拟页
    for (int i = 0; i < PAGE_TABLE_SIZE; ++i) {
        if (page_table[i].p_id == p_addr && page_table[i].owner == pid) {
            // 将内存数据写入磁盘
            memcpy(&disk[page_table[i].v_id * PAGE_SIZE],
                   &memory[p_addr * PAGE_SIZE],
                   PAGE_SIZE);
            
            // 更新页表项
            page_table[i].in_memory = false;
            page_table[i].p_id = FULL;
            p_page[p_addr / 8] &= ~(1 << (p_addr % 8));
            return 0;
        }
    }
    return -1; // 未找到对应页
}

void test_memory() {
    cout << "=== Testing Memory Management ===" << endl;
    init_memory();

    // 测试1：基本内存分配与释放
    cout << "\n[Test 1] Basic allocation and free:" << endl;
    m_pid pid1 = 1;
    v_address addr1 = alloc_for_process(pid1, 8192);
    cout << "Process 1 allocated at: " << addr1 << endl;
    free_process_memory(pid1);
    cout << "Freed process 1 memory" << endl;

    // 测试2：多进程内存分配
    cout << "\n[Test 2] Multi-process allocation:" << endl;
    m_pid pid2 = 2;
    v_address addr2 = alloc_for_process(pid2, 16384);
    cout << "Process 2 allocated at: " << addr2 << endl;
    m_pid pid3 = 3;
    v_address addr3 = alloc_for_process(pid3, 32768);
    cout << "Process 3 allocated at: " << addr3 << endl;

    // 测试3：内存读写操作
    cout << "\n[Test 3] Memory read/write operations:" << endl;
    atom_data data;
    write_memory(0xAA, addr2, pid2);
    read_memory(&data, addr2, pid2);
    cout << "Read from process 2: " << (int)data << endl;

    // 测试4：设备内存分配
    cout << "\n[Test 4] Device memory allocation:" << endl;
    v_address dev_addr = alloc_for_device(1, 4096);
    cout << "Device buffer allocated at: " << dev_addr << endl;

    // 测试5：文件内存分配与释放
    cout << "\n[Test 5] File memory allocation:" << endl;
    v_address file_addr;
    if (alloc_for_file(8192, &file_addr) == 0) {
        cout << "File memory allocated at: " << file_addr << endl;
        free_file_memory(file_addr);
        cout << "Freed file memory" << endl;
    }

    // 测试6：页面置换
    cout << "\n[Test 6] Page replacement:" << endl;
    // 分配大量内存以触发页面置换
    for (int i = 0; i < 100; i++) {
        alloc_for_process(4 + i, 65536);
    }
    cout << "Page replacement triggered" << endl;

    // 测试7：内存状态报告
    cout << "\n[Test 7] Memory status report:" << endl;
    sendMemoryStatusToUI();

    // 清理
    free_process_memory(pid2);
    free_process_memory(pid3);
    for (int i = 0; i < 100; i++) {
        free_process_memory(4 + i);
    }
}

void test_filesystem() {
    cout << "\n=== Testing File System ===" << endl;
    FileSystem fs(1024, 4096);

    // 测试1：基础读写测试
    cout << "\n[Test 1] Basic read/write:" << endl;
    fs.createFile("/", "data.bin", FILE_TYPE, 1024);
    
    // 写入精确大小的数据
    string testData(1024, 'A');  // 1024字节全'A'
    fs.writeFile("/", "data.bin", testData);
    // 读取验证
    string content = fs.readFile("/", "data.bin");
    cout << "File content: " << content << endl;
    cout << "Data verification: " 
         << (content == testData ? "Passed" : "Failed") 
         << endl;

    // 测试2：覆盖写入测试
    cout << "\n[Test 2] Overwrite test:" << endl;
    string newData(512, 'B');
    fs.writeFile("/", "data.bin", newData);
    
    content = fs.readFile("/", "data.bin");
    cout << "Overwrite result: "
         << (content.substr(0,512) == string(512, 'B') && 
             content.substr(512) == string(512, 'A') ? "Passed" : "Failed")
         << endl;
    
    // 输出data.bin内容
    cout << "File content: " << content << endl;

    // 测试3：边界条件测试
    cout << "\n[Test 3] Boundary condition test:" << endl;
    // 创建正好占满一个块的文件（4096字节）
    fs.createFile("/", "4kfile", FILE_TYPE, 4096);
    string fullBlock(4096, 'C');
    int writeResult = fs.writeFile("/", "4kfile", fullBlock);
    cout << "Full block write: " 
         << (writeResult == 0 ? "Success" : "Failed") 
         << endl;

    // 测试4：大文件测试
    cout << "\n[Test 4] Large file test:" << endl;
    const int LARGE_SIZE = 16 * 4096;  // 16个块
    fs.createFile("/", "large.bin", FILE_TYPE, LARGE_SIZE);
    
    string largeData(LARGE_SIZE, 'D');
    writeResult = fs.writeFile("/", "large.bin", largeData);
    cout << "Large file write: "
         << (writeResult == 0 ? "Success" : "Failed") 
         << " (" << LARGE_SIZE << " bytes)" 
         << endl;

    // 测试5：空间回收验证
    cout << "\n[Test 5] Space reclamation:" << endl;
    fs.deleteFile("/", "data.bin");
    fs.deleteFile("/", "4kfile");
    cout << "Deleted two files" << endl;
    
    // 验证空间是否回收
    cout << "Final free space list:" << endl;
    fs.printFreeSpaceList();
}

void test_directory_operations() {
    FileSystem fs(1024, 4096);

    // 创建目录
    fs.createDirectory("/", "documents");
    fs.printDirectory("/documents"); // 应显示空目录

    // 创建文件（仅一次）
    int createResult = fs.createFile("/documents", "notes.txt", FILE_TYPE, 1024);
    if (createResult == 0) {
        fs.printDirectory("/documents");
    } else {
        cerr << "文件创建失败，错误码: " << createResult << endl;
        return; // 提前终止以排查问题
    }

    // 创建子目录
    fs.createDirectory("/documents", "projects");
    fs.createDirectory("/documents", "1111");
    fs.printDirectory("/documents"); // 应显示文件和目录

    // 写入文件内容
    fs.writeFile("/documents", "notes.txt", "Project meeting notes...");
    int writeResult = fs.writeFile("/documents", "notes.txt", "Project meeting notes...");
    if (writeResult == 0) {
        cout << "\n文件写入成功";
        
        // 读取并展示内容
        cout << "\n文件内容预览：";
        string content = fs.readFile("/documents", "notes.txt");
        cout << "\n--------------------------------------------------";
        cout << "\n" << content.substr(0, 100) << "..." << endl; // 显示前100字符
        cout << "--------------------------------------------------\n";
    } else {
        cout << "\n文件写入失败";
    }
    
    // 递归删除
    cout << "\nDeleting /documents recursively..." << endl;
    
    //删除子目录
    fs.deleteDirectoryRecursive("/documents/projects");
    fs.printDirectory("/documents");
    
    // 最终验证
    fs.printDirectory("/");
}

//发送内存状态给UI
int sendMemoryStatusToUI() {
    //添加与UI通信的代码
    //例如：统计并返回内存使用情况、页面置换次数等信息
    return 0;
}

/*int main() {
    SetConsoleOutputCP(CP_UTF8);  // 设置控制台输出为 UTF-8 编码
    test_memory();
    //test_filesystem();
    //test_directory_operations();
    
    return 0;
}*/