// memory.cpp
#include <iostream>
#include "../include/memory.h"
#include <Windows.h>
#include "../include/filesystem.h"
#include <vector>
#include <algorithm>
#include <WinNls.h>
#include <string>
#include <iomanip>

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
atom_data memory[MEMORY_SIZE] = { 0 };
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

        // 新增帧信息关联
        Frame* current = clock_hand;
        do {
            if (current->p_id == pt.p_id) {
                current->owner = pid;       // 设置帧所有者
                current->v_id = v_page_num; // 绑定虚拟页号
                current->used = true;       // 标记为已使用
                break;
            }
            current = current->next;
        } while (current != clock_hand);
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
            cerr << "[ERROR] No available physical page for page in" << std::endl;
            return -1;
        }
        std::cout << "[DEBUG] Page replacement triggered: Victim page " << p_page_num << " (Virtual page " << v_page_num << ")" << std::endl;
    }

    // 更新页表项
    pt.p_id = p_page_num;
    pt.in_memory = true;
    p_page[p_page_num / 8] |= 1 << (p_page_num % 8);

    // 从磁盘中恢复页面内容到内存
    memcpy(&memory[p_page_num * PAGE_SIZE],
           &disk[v_page_num * PAGE_SIZE],
           PAGE_SIZE);
    std::cout << "[DEBUG] Page in: Virtual page " << v_page_num << " -> Physical page " << p_page_num << std::endl;
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
                    std::cout << "[DEBUG] Page out: Physical page " << victim << " -> Virtual page " << current->v_id << std::endl;
                    return victim;
                }
            }
            cerr << "[ERROR] Failed to find corresponding virtual page for physical page " << victim << std::endl;
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

    // 初始化后设置时钟指针为有效帧
    while (clock_hand->p_id >= P_PAGE_USE_SIZE) {
        clock_hand = clock_hand->next;
    }
}
void Pagefault(int pid, int v_addr, std::string info, int* data, int flag) {
    // 处理缺页中断的逻辑
    std::cout << "[PAGEFAULT] Process " << pid << " triggered a page fault at virtual address: " << v_addr << std::endl;
    std::cout << "Info: " << info << std::endl;
    std::cout << "Flag: " << flag << std::endl;
    // 调用 page_in 函数将页面加载到内存
    if (page_in(v_addr, pid) != 0) {
        std::cerr << "[ERROR] Failed to handle page fault for process " << pid << std::endl;
    }
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
    if (!pt.in_memory) {
        Pagefault(pid, address, "Page not in memory during read", nullptr, 0);
        return -1; // 返回错误码
    } // 页不在内存中
    *data = memory[pt.p_id * PAGE_SIZE + (address % PAGE_SIZE)];
    pt.used = true; // 标记为访问过
    return 0;
}

// 将一个字节写入到指定的虚拟地址
int write_memory(atom_data data, v_address address, m_pid pid) {
    page v_page_num = address / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];
    if (!pt.in_memory) {
        Pagefault(pid, address, "Page not in memory during write", nullptr, 0);
        return -1; // 返回错误码
    }
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
//地址转换
int translate_address(v_address v_addr, m_pid pid, p_address* p_addr) {
    page v_page_num = v_addr / PAGE_SIZE;

    // 检查虚拟页号是否在有效范围内
    if (v_page_num >= V_PAGE_USE_SIZE) {
        cerr << "[ERROR] Virtual page number out of range: " << v_page_num << std::endl;
        return -1;
    }
    
    PageTableItem& pt = page_table[v_page_num];
    
    // 检查页面所有权
    if (pt.owner != pid) {
        cerr << "[ERROR] Process " << pid << " does not own page " << v_page_num << std::endl;
        return -1;
    }
    
    // 如果页面不在内存中，返回缺页错误
    if (!pt.in_memory) {
        Pagefault(pid, v_addr, "Page not in memory", nullptr, 0);
        return -2; // 特别标识缺页错误
    }
    // 计算物理地址
    *p_addr = pt.p_id * PAGE_SIZE + (v_addr % PAGE_SIZE);
    
    // 更新访问位
    pt.used = true;
    
    std::cout << "[DEBUG] Address translation: Virtual 0x" << std::hex << v_addr 
              << " -> Physical 0x" << *p_addr << std::dec << std::endl;
    
    return 0;
    }
int read_instruction(char* instruction_buffer, size_t max_size, v_address v_addr, m_pid pid, size_t* bytes_read) {
        // 首先尝试进行地址翻译
        p_address p_addr;
        int result = translate_address(v_addr, pid, &p_addr);
        
        if (result == -2) { // 缺页情况
            // 触发缺页中断
            Pagefault(pid, v_addr, "Page fault during instruction read", nullptr, 0);
            return -1;
        } else if (result != 0) { // 其他错误
            std::cerr << "[ERROR] Failed to translate address 0x" << std::hex << v_addr << std::dec << std::endl;
            return -1;
        }
        
        // 地址翻译成功，现在从物理地址读取数据
        size_t available_in_page = PAGE_SIZE - (v_addr % PAGE_SIZE);
        size_t bytes_to_read = std::min<size_t>(max_size - 1, available_in_page); // 保留一个位置给字符串终止符
        
        // 从物理内存读取数据
        memcpy(instruction_buffer, &memory[p_addr], bytes_to_read);
        
        // 查找换行符
        char* newline_pos = static_cast<char*>(memchr(instruction_buffer, '\n', bytes_to_read));
        
        if (newline_pos) {
            // 找到换行符，读取一条完整指令
            *newline_pos = '\0'; // 终止字符串
            *bytes_read = newline_pos - instruction_buffer + 1; // 包括原换行符的位置
            return 0; // 成功读取完整指令
        } else {
            // 未找到换行符，需要继续读取更多数据
            *bytes_read = bytes_to_read;
            return 1; // 标识需要更多数据
        }
    }

void print_memory_usage() {
    // 物理页统计
    size_t physical_free = 0;
    std::cout << "=== Memory Usage Report ===" << std::endl;
    std::cout << "Physical Pages: ";
    for (int i = 0; i < P_PAGE_USE_SIZE; ++i) {
        bool is_free = !(p_page[i / 8] & (1 << (i % 8)));
        if (is_free) ++physical_free;

        std::cout << "[P" << std::setw(2) << i << ": " 
             << (is_free ? "❌" : "✅") << "] ";
        if ((i + 1) % 4 == 0) std::cout << std::endl << "              ";
    }
    std::cout << std::endl;
    std::cout << "Physical Memory Usage: "
         << (P_PAGE_USE_SIZE - physical_free) << "/" 
         << P_PAGE_USE_SIZE << " pages used (" 
         << (P_PAGE_USE_SIZE - physical_free) * PAGE_SIZE / 1024 << "KB/" 
         << P_PAGE_USE_SIZE * PAGE_SIZE / 1024 << "KB)" << std::endl;

    // 虚拟页统计
    size_t virtual_free = 0;
    std::cout << "Virtual Pages:  ";
    for (int i = 0; i < V_PAGE_USE_SIZE; ++i) {
        bool is_free = !(v_page[i / 8] & (1 << (i % 8)));
        if (is_free) ++virtual_free;

        std::cout << "[P" << std::setw(2) << i << ": " 
             << (is_free ? "❌" : "✅") << "] ";
        if ((i + 1) % 4 == 0) std::cout << std::endl << "              ";
    }
    std::cout << std::endl;
    std::cout << "Virtual Memory Usage:  "
         << (V_PAGE_USE_SIZE - virtual_free) << "/" 
         << V_PAGE_USE_SIZE << " pages used (" 
         << (V_PAGE_USE_SIZE - virtual_free) * PAGE_SIZE / 1024 << "KB/" 
         << V_PAGE_USE_SIZE * PAGE_SIZE / 1024 << "KB)" << std::endl;
}
//发送内存状态给UI
int sendMemoryStatusToUI() {
    //添加与UI通信的代码
    //例如：统计并返回内存使用情况、页面置换次数等信息
    return 0;
}
void test_memory1() {
    std::cout << "=== Testing Memory Management ===" << std::endl;
    init_memory();

    // 测试1：基本内存分配与释放
    std::cout << "\n[Test 1] Basic allocation and free:" << std::endl;
    m_pid pid1 = 1;
    v_address addr1 = alloc_for_process(pid1, 8192);
    std::cout << "Process 1 allocated at: " << addr1 / PAGE_SIZE << " (page number)" << std::endl;
    free_process_memory(pid1);
    std::cout << "Freed process 1 memory" << std::endl;
    print_memory_usage();
    

    // 测试2：多进程内存分配
    std::cout << "\n[Test 2] Multi-process allocation:" << std::endl;
    m_pid pid2 = 2;
    v_address addr2 = alloc_for_process(pid2, 16384);
    std::cout << "Process 2 allocated at: " << addr2 / PAGE_SIZE << " (page number)" << std::endl;
    m_pid pid3 = 3;
    v_address addr3 = alloc_for_process(pid3, 8192);
    std::cout << "Process 3 allocated at: " << addr3 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();


    // 测试3：内存读写操作
    std::cout << "\n[Test 3] Memory read/write operations:" << std::endl;
    atom_data data;
    write_memory(0xAA, addr2, pid2);
    read_memory(&data, addr2, pid2);
    std::cout << "Read from process 2: " << (int)data << std::endl;

    /*// 测试4：设备内存分配
    std::cout << "\n[Test 4] Device memory allocation:" << std::endl;
    v_address dev_addr = alloc_for_device(1, 4096);
    std::cout << "Device buffer allocated at: " << dev_addr / PAGE_SIZE << " (page number)" << std::endl;*/

    // 测试5：文件内存分配与释放
    std::cout << "\n[Test 5] File memory allocation:" << std::endl;
    v_address file_addr;
    if (alloc_for_file(8192, &file_addr) == 0) {
        std::cout << "File memory allocated at: " << file_addr / PAGE_SIZE << " (page number)" << std::endl;
        //free_file_memory(file_addr);
        //std::cout << "Freed file memory" << std::endl;
    }
    print_memory_usage();
    
    // 测试6：页面置换 + 页面换入验证
    std::cout << "\n[Test 6] Page replacement and page in verification:" << std::endl;

    // 分配大量内存以触发页面置换
    v_address addr4 = alloc_for_process(4, 4096);
    std::cout << "Process 4 allocated at: " << addr4 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr5 = alloc_for_process(5, 4096);
    std::cout << "Process 5 allocated at: " << addr5 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr6 = alloc_for_process(6, 4096);
    std::cout << "Process 6 allocated at: " << addr6 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr7 = alloc_for_process(7, 4096);
    std::cout << "Process 7 allocated at: " << addr7 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr8 = alloc_for_process(8, 8192);
    std::cout << "Process 8 allocated at: " << addr8 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr9 = alloc_for_process(9, 8192);
    std::cout << "Process 9 allocated at: " << addr9 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr10 = alloc_for_process(10, 8192);
    std::cout << "Process 10 allocated at: " << addr10 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr11 = alloc_for_process(11, 4096);
    std::cout << "Process 11 allocated at: " << addr11 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();


    page_in(addr2,2);
    read_memory(&data, addr2, pid2);
    std::cout << "Read from process 2: " << (int)data << std::endl;
    print_memory_usage();

    /*// 向这些地址写入数据，确保会被换出到磁盘
    std::string testData = "This is test data for page replacement and re-load from disk!";
    for (size_t i = 0; i < testData.size(); ++i) {
        write_memory(static_cast<atom_data>(testData[i]), addr4 + i, 4);
    }
    std::cout << "[DEBUG] Wrote test data to process 4's memory." << std::endl;

    // 手动触发一次 page_in 操作来模拟缺页中断
    std::cout << "[INFO] Simulating page fault for process 4..." << std::endl;
    int result = page_in(addr4, 4); // 将虚拟地址对应的页面重新加载进内存

    if (result == 0) {
        std::cout << "[SUCCESS] Page in successful." << std::endl;

    // 验证数据是否正确恢复
    std::string recoveredData;
    recoveredData.resize(testData.size());
    for (size_t i = 0; i < testData.size(); ++i) {
        atom_data data;
        read_memory(&data, addr4 + i, 4);
        recoveredData[i] = static_cast<char>(data);
    }

    std::cout << "[DEBUG] Recovered data: " << recoveredData << std::endl;
    if (recoveredData == testData) {
        std::cout << "[RESULT] Data integrity verified after page replacement. ✅" << std::endl;
    } else {
        std::cerr << "[ERROR] Data mismatch after page replacement! ❌" << std::endl;
    }
    } else {
        std::cerr << "[ERROR] Failed to page in virtual address 0x" << std::hex << addr4 << std::endl;
    } 

    std::cout << "Page replacement and page-in verification complete." << std::endl;*/
    
    /*// 测试6：页面置换
    std::cout << "\n[Test 6] Page replacement:" << std::endl;
    // 分配大量内存以触发页面置换
    for (int i = 0; i < 3; i++) {
        alloc_for_process(4+i, 4096);
        print_memory_usage();
    }
    std::cout << "Page replacement triggered" << std::endl;*/

    // 测试7：内存状态报告
    std::cout << "\n[Test 7] Memory status report:" << std::endl;
    sendMemoryStatusToUI();

    // 清理
    free_process_memory(pid2);
    free_process_memory(pid3);
    for (int i = 0; i < 100; i++) {
        free_process_memory(4 + i);
    }
}

void test_memory2() {
    std::cout << "=== Enhanced Memory Management Test ===" << std::endl;
    init_memory();
    // 测试2：内存分配压力测试
    std::cout << "\n[Test 2] Stress test with multiple processes:" << std::endl;
    const int PROCESS_COUNT = 10;
    vector<m_pid> pids;
    for (int i = 0; i < PROCESS_COUNT; i++) {
        m_pid pid = 2000 + i;
        v_address addr = alloc_for_process(pid, (i%3 + 1)*PAGE_SIZE);
        if (addr != FULL) {
            pids.push_back(pid);
            std::cout << "Process " << pid << " allocated "
                 << dec << (i%3 +1) << " page(s) at 0x"
                 << hex << addr << std::endl;
        }
    }
    
    // 资源清理
    std::cout << "\n[Cleanup] Releasing all allocated resources..." << std::endl;
    for (auto pid : pids) free_process_memory(pid);
    
}

void test_address_translation(){
    // 初始化内存系统
    init_memory();
    
    // 测试数据大小（1页）
    m_size size = PAGE_SIZE;
    
    // 进程ID
    m_pid pid = 100;
    
    // 分配虚拟内存
    v_address v_addr = alloc_for_process(pid, size);
    if (v_addr == FULL) {
        std::cerr << "Failed to allocate virtual memory" << std::endl;
        return;
    }
    
    std::cout << "Virtual address allocated: 0x" << std::hex << v_addr << std::dec << std::endl;
    
    // 获取虚拟页号
    page v_page_num = v_addr / PAGE_SIZE;
    std::cout << "Virtual page number: " << static_cast<int>(v_page_num) << std::endl;
    
    // 获取页表项
    PageTableItem& pt = page_table[v_page_num];
    
    // 等待页面被加载到内存（在实际系统中可能需要处理缺页中断）
    // 这里因为alloc_for_process已经分配了物理页，所以可以直接访问
    
    // 获取物理页号
    p_address p_page_num = pt.p_id;
    std::cout << "Physical page number: " << static_cast<int>(p_page_num) << std::endl;
    
    // 计算物理地址 
    p_address calculated_p_addr = pt.p_id * PAGE_SIZE + (v_addr % PAGE_SIZE);
    
    // 输出结果
    std::cout << "\n[Address Translation Result]" << std::endl;
    std::cout << "Virtual Address:  0x" << std::hex << v_addr << std::dec << std::endl;
    std::cout << "Virtual Page:     " << static_cast<int>(v_page_num) << std::endl;
    std::cout << "Physical Page:    " << static_cast<int>(p_page_num) << std::endl;
    std::cout << "Page Offset:      0x" << std::hex << (v_addr % PAGE_SIZE) << std::dec << std::endl;
    std::cout << "Calculated PA:    0x" << std::hex << calculated_p_addr << std::dec << std::endl;
    // 清理
    free_process_memory(pid);
    std::cout << "\nTest completed and memory freed" << std::endl;
}
void test_memory_swap() {
    std::cout << "\n=== Testing Memory Swap (Page In from Disk) ===" << std::endl;
    init_memory();

    // 模拟分配虚拟内存
    m_pid pid = 1;
    v_address addr = alloc_for_process(pid, 8192);
    std::cout << "Process 1 allocated virtual memory at: " << addr << std::endl;

    // 模拟写入数据到虚拟内存
    string testData = "Hello, this is a test data for page swap!";
    for (size_t i = 0; i < testData.size(); ++i) {
        write_memory(static_cast<atom_data>(testData[i]), addr + i, pid);
    }
    std::cout << "Data written to virtual memory: " << testData << std::endl;

    // 模拟页面换出到磁盘
    page v_page_num = addr / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];
    if (pt.in_memory) {
        memcpy(&disk[v_page_num * PAGE_SIZE], &memory[pt.p_id * PAGE_SIZE], PAGE_SIZE);
        pt.in_memory = false;
        p_page[pt.p_id / 8] &= ~(1 << (pt.p_id % 8));
        std::cout << "[DEBUG] Page out: Virtual page " << v_page_num << " -> Disk" << std::endl;
    }

    // 模拟页面换入
    int result = page_in(addr, pid);
    if (result == 0) {
        std::cout << "Page in successful" << std::endl;

        // 验证换入的数据
        string recoveredData;
        recoveredData.resize(testData.size());
        for (size_t i = 0; i < testData.size(); ++i) {
            atom_data data;
            read_memory(&data, addr + i, pid);
            recoveredData[i] = static_cast<char>(data);
        }
        std::cout << "Recovered data from memory: " << recoveredData << std::endl;
        std::cout << "Data verification: " << (recoveredData == testData ? "Passed" : "Failed") << std::endl;
    } else {
        cerr << "Page in failed" << std::endl;
    }

    // 清理
    free_process_memory(pid);
}
void test_filesystem() {
    std::cout << "\n=== Testing File System ===" << std::endl;
    FileSystem fs(1024, 4096);

    // 测试1：基础读写测试
    std::cout << "\n[Test 1] Basic read/write:" << std::endl;
    fs.createFile("/", "data.bin", FILE_TYPE, 1024);
    
    // 写入精确大小的数据
    string testData(1024, 'A');  // 1024字节全'A'
    fs.writeFile("/", "data.bin", testData);
    // 读取验证
    string content = fs.readFile("/", "data.bin");
    std::cout << "File content: " << content << std::endl;
    std::cout << "Data verification: " 
         << (content == testData ? "Passed" : "Failed") 
         << std::endl;

    // 测试2：覆盖写入测试
    std::cout << "\n[Test 2] Overwrite test:" << std::endl;
    string newData(512, 'B');
    fs.writeFile("/", "data.bin", newData);
    
    content = fs.readFile("/", "data.bin");
    std::cout << "Overwrite result: "
         << (content.substr(0,512) == string(512, 'B') && 
             content.substr(512) == string(512, 'A') ? "Passed" : "Failed")
         << std::endl;
    
    // 输出data.bin内容
    std::cout << "File content: " << content << std::endl;

    // 测试3：边界条件测试
    std::cout << "\n[Test 3] Boundary condition test:" << std::endl;
    // 创建正好占满一个块的文件（4096字节）
    fs.createFile("/", "4kfile", FILE_TYPE, 4096);
    string fullBlock(4096, 'C');
    int writeResult = fs.writeFile("/", "4kfile", fullBlock);
    std::cout << "Full block write: " 
         << (writeResult == 0 ? "Success" : "Failed") 
         << std::endl;

    // 测试4：大文件测试
    std::cout << "\n[Test 4] Large file test:" << std::endl;
    const int LARGE_SIZE = 16 * 4096;  // 16个块
    fs.createFile("/", "large.bin", FILE_TYPE, LARGE_SIZE);
    
    string largeData(LARGE_SIZE, 'D');
    writeResult = fs.writeFile("/", "large.bin", largeData);
    std::cout << "Large file write: "
         << (writeResult == 0 ? "Success" : "Failed") 
         << " (" << LARGE_SIZE << " bytes)" 
         << std::endl;

    // 测试5：空间回收验证
    std::cout << "\n[Test 5] Space reclamation:" << std::endl;
    fs.deleteFile("/", "data.bin");
    fs.deleteFile("/", "4kfile");
    std::cout << "Deleted two files" << std::endl;
    
    // 验证空间是否回收
    std::cout << "Final free space list:" << std::endl;
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
        cerr << "文件创建失败，错误码: " << createResult << std::endl;
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
        std::cout << "\n文件写入成功";
        
        // 读取并展示内容
        std::cout << "\n文件内容预览：";
        string content = fs.readFile("/documents", "notes.txt");
        std::cout << "\n--------------------------------------------------";
        std::cout << "\n" << content.substr(0, 100) << "..." << std::endl; // 显示前100字符
        std::cout << "--------------------------------------------------\n";
    } else {
        std::cout << "\n文件写入失败";
    }
    
    // 递归删除
    std::cout << "\nDeleting /documents recursively..." << std::endl;
    
    //删除子目录
    fs.deleteDirectoryRecursive("/documents/projects");
    fs.printDirectory("/documents");
    
    // 最终验证
    fs.printDirectory("/");
}

/*int main() {
    SetConsoleOutputCP(CP_UTF8);  // 设置控制台输出为 UTF-8 编码
    //test_memory1();
    test_memory2();
    //test_address_translation();
    //test_memory_swap();
    //test_filesystem();
    //test_directory_operations();
    
    return 0;
}*/