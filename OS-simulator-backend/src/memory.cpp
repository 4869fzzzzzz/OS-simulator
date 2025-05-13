// memory.cpp
#include <iostream>
#include "../include/memory.h"
#include <Windows.h>
#include "../include/filesystem.h"

#include <WinNls.h>
#include <string>
#include <iomanip>
#include "../include/interrupt.h"
#include <sstream>


using namespace std;
using namespace aigc;

// ---------------- 全局变量定义 ----------------
// 全局计数器模拟页面置换次数
size_t g_page_replacement_count = 0;
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
    g_page_replacement_count++;
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

int page_out(v_address v_addr, m_pid pid)
{
    // 计算虚拟页号
    page v_page_num = v_addr / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];
    
    // 检查页面所有权和是否在内存中
    if (pt.owner != pid) {
        cerr << "[ERROR] Process " << pid << " does not own page " << v_page_num << std::endl;
        return 0;
    }
    
    if (!pt.in_memory) {
        cerr << "[ERROR] Page " << v_page_num << " is not in memory" << std::endl;
        return 0;
    }
    
    // 获取物理页号
    page p_page_num = pt.p_id;
    
    // 将页面内容复制到磁盘
    memcpy(&disk[v_page_num * PAGE_SIZE],
           &memory[p_page_num * PAGE_SIZE],
           PAGE_SIZE);
           
    // 更新页表项
    pt.in_memory = false;
    pt.p_id = FULL;
    
    // 更新物理页位图，标记为可用
    p_page[p_page_num / 8] &= ~(1 << (p_page_num % 8));
    
    // 更新相关的帧信息
    Frame* current = clock_hand;
    do {
        if (current->p_id == p_page_num) {
            current->owner = FULL;
            current->v_id = FULL;
            current->used = false;
            break;
        }
        current = current->next;
    } while (current != clock_hand);
    
    std::cout << "[DEBUG] Page out: Virtual page " << v_page_num 
              << " -> Disk, Physical page " << p_page_num << " freed" << std::endl;
    
    return 1;
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
        raiseInterrupt(InterruptType::PAGEFAULT,pid, address , "Page not in memory during read", nullptr, 0);
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
        raiseInterrupt(InterruptType::PAGEFAULT,pid, address , "Page not in memory during write", nullptr, 0);
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
int alloc_for_file(m_size size, v_address* addr, m_pid owner) {
    v_address allocated = alloc_for_process(owner, size); // 使用指定进程ID分配内存
    if (allocated == FULL) return -1;
    *addr = allocated;
    return 0;
}

//释放文件占用的内存
void free_file_memory(v_address addr, m_pid owner) {
    page v_page_num = addr / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];

    if (pt.owner == owner) { // 确保是该进程分配的文件内存
        v_page[v_page_num / 8] &= ~(1 << (v_page_num % 8));
        if (pt.in_memory) {
            p_page[pt.p_id / 8] &= ~(1 << (pt.p_id % 8));
        }
        memset(&pt, 0, sizeof(PageTableItem));
    } else {
        std::cerr << "[ERROR] Cannot free memory: Not owned by this process." << std::endl;
        return;
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
void Pagefault(int pid, int v_addr, std::string info, int* data, int flag) {
        // 处理缺页中断的逻辑
        std::cout << "[PAGEFAULT] Process " << pid << " triggered a page fault at virtual address: " << v_addr << std::endl;
        std::cout << "Info: " << info << std::endl;
        std::cout << "Flag: " << flag << std::endl;
    
        // 如果需要，可以通过 data 指针返回数据
        if (data) {
            *data = 0; // 示例：返回一个默认值
        }
    
        // 调用 page_in 函数将页面加载到内存
        if (page_in(v_addr, pid) != 0) {
            std::cerr << "[ERROR] Failed to handle page fault for process " << pid << std::endl;
        }
    }

//0 成功读取一行指令;-1 缺页异常;-2 缺页异常
int read_instruction(char* instruction_buffer, size_t max_size, v_address v_addr, m_pid pid, size_t* bytes_read) {
        p_address p_addr;
        int result = translate_address(v_addr, pid, &p_addr);
    
        if (result == -2) { // 缺页情况
            std::cout << "[INFO] Page fault at address 0x" << std::hex << v_addr << std::dec << std::endl;
    
            // 调用 page_in 处理缺页
            if (page_in(v_addr, pid) != 0) {
                std::cerr << "[ERROR] Failed to resolve page fault for address 0x"
                          << std::hex << v_addr << std::dec << std::endl;
                raiseInterrupt(InterruptType::PAGEFAULT, pid, v_addr, "Page fault during instruction read", nullptr, 0);
                return -1;
            }
    
            // 成功换入页面后重新翻译地址
            result = translate_address(v_addr, pid, &p_addr);
            if (result != 0) {
                std::cerr << "[ERROR] Address translation failed after page in for 0x"
                          << std::hex << v_addr << std::dec << std::endl;
                return -1;
            }
        } else if (result != 0) { // 其他错误
            std::cerr << "[ERROR] Failed to translate address 0x" << std::hex << v_addr << std::dec << std::endl;
            return -1;
        }
    
        // 计算当前虚拟地址在物理内存中的偏移量
        size_t available_in_page = PAGE_SIZE - (v_addr % PAGE_SIZE);
        size_t bytes_to_read = std::min<size_t>(max_size - 1, available_in_page);
    
        // 从物理内存复制数据到缓冲区
        memcpy(instruction_buffer, &memory[p_addr], bytes_to_read);
    
        // 查找换行符
        char* newline_pos = static_cast<char*>(memchr(instruction_buffer, '\n', bytes_to_read));
    
        if (newline_pos) {
            *newline_pos = '\0'; // 终止字符串
            *bytes_read = newline_pos - instruction_buffer + 1; // 包括换行符
            return 0;
        } else {
            // 如果没有找到换行符且剩余字节数为零，则表示已到达文件尾
            if (bytes_to_read == 0) {
                *bytes_read = 0;
                return -2;
            }
            *bytes_read = bytes_to_read;
            return -2;
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

void fillMemoryStatus(MemoryStatusForUI& status) {
    // 填充总览信息
    status.overview.page_size = PAGE_SIZE;
    status.overview.total_physical_mem = MEMORY_SIZE;
    status.overview.swap_total = DISK_SIZE;

    // 计算已使用/可用物理内存
    size_t physical_free = 0;
    for (int i = 0; i < P_PAGE_USE_SIZE; ++i) {
        if (!(p_page[i / 8] & (1 << (i % 8)))) {
            ++physical_free;
        }
    }
    status.overview.used_physical_mem = (P_PAGE_USE_SIZE - physical_free) * PAGE_SIZE;
    status.overview.free_physical_mem = physical_free * PAGE_SIZE;

    // 计算交换区使用量（遍历每个虚拟页是否在磁盘上）
    size_t swap_used = 0;
    for (int i = 0; i < V_PAGE_USE_SIZE; ++i) {
        if (!page_table[i].in_memory && page_table[i].owner != FULL) {
            ++swap_used;
        }
    }
    status.overview.swap_used = swap_used * PAGE_SIZE;
    status.overview.page_replacement_count = g_page_replacement_count;

    // 页面信息
    status.page_info.total_physical_pages = P_PAGE_USE_SIZE;
    status.page_info.used_physical_pages = P_PAGE_USE_SIZE - physical_free;
    status.page_info.total_virtual_pages = V_PAGE_USE_SIZE;

    // 置换算法信息
    status.replacement_info.current_clock_hand = clock_hand ? clock_hand->p_id : -1;
    static int last_page_in = -1;
    static int last_page_out = -1;
    status.replacement_info.last_page_in = last_page_in;
    status.replacement_info.last_page_out = last_page_out;
    status.replacement_info.replacement_count = g_page_replacement_count;

    // 进程内存映射表
    for (int i = 0; i < PAGE_TABLE_SIZE; ++i) {
        const PageTableItem& pt = page_table[i];
        if (pt.owner == FULL || pt.v_id == FULL) continue;

        ProcessMemoryMappingItem item;
        item.pid = pt.owner;
        item.page_count = 1;

        // 虚拟地址范围
        v_address v_start = pt.v_id * PAGE_SIZE;
        v_address v_end = v_start + PAGE_SIZE - 1;
        std::stringstream ss_v;
        ss_v << "0x" << std::hex << v_start << "-0x" << v_end;
        item.v_address_range = ss_v.str();

        // 物理地址范围
        if (pt.in_memory) {
            p_address p_start = pt.p_id * PAGE_SIZE;
            p_address p_end = p_start + PAGE_SIZE - 1;
            std::stringstream ss_p;
            ss_p << "0x" << std::hex << p_start << "-0x" << p_end;
            item.p_address_range = ss_p.str();
            item.status = "in_memory";
        } else {
            item.p_address_range = "N/A";
            item.status = "swapped_out";
        }

        status.process_mappings.push_back(item);
    }
}

void sendMemoryStatusToUI() {
    MemoryStatusForUI status;
    fillMemoryStatus(status);

    // 使用 AIGC_JSON_HELPER 的序列化能力将 status 转换为 JSON 字符串
    std::string jsonStr;
    JsonHelper::ObjectToJson(status, jsonStr);
    
    std::cout << "Generated JSON:\n" << jsonStr << std::endl;
}


void test_memory1() {
    std::cout << "=== Testing Memory Management ===" << std::endl;
    init_memory();

    // 测试1：基本内存分配与释放
    std::cout << "\n[Test 1] Basic allocation and free:" << std::endl;
    m_pid pid1 = 1;
    v_address addr1 = alloc_for_process(pid1, 1024);
    std::cout << "Process 1 allocated at: " << addr1 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();
    free_process_memory(pid1);
    std::cout << "Freed process 1 memory" << std::endl;
    print_memory_usage();
    

    // 测试2：多进程内存分配
    std::cout << "\n[Test 2] Multi-process allocation:" << std::endl;
    m_pid pid2 = 2;
    v_address addr2 = alloc_for_process(pid2, 4096);
    std::cout << "Process 2 allocated at: " << addr2 / PAGE_SIZE << " (page number)" << std::endl;
    m_pid pid3 = 3;
    v_address addr3 = alloc_for_process(pid3, 2048);
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
    if (alloc_for_file(2048, &file_addr) == 0) {
        std::cout << "File memory allocated at: " << file_addr / PAGE_SIZE << " (page number)" << std::endl;
        //free_file_memory(file_addr);
        //std::cout << "Freed file memory" << std::endl;
    }
    print_memory_usage();
    
    // 测试6：页面置换 + 页面换入验证
    std::cout << "\n[Test 6] Page replacement and page in verification:" << std::endl;

    // 分配大量内存以触发页面置换
    v_address addr4 = alloc_for_process(4, 1024);
    std::cout << "Process 4 allocated at: " << addr4 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr5 = alloc_for_process(5, 1024);
    std::cout << "Process 5 allocated at: " << addr5 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr6 = alloc_for_process(6, 1024);
    std::cout << "Process 6 allocated at: " << addr6 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    free_process_memory(pid2);
    print_memory_usage();

    v_address addr7 = alloc_for_process(7, 1024);
    std::cout << "Process 7 allocated at: " << addr7 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr8 = alloc_for_process(8, 2048);
    std::cout << "Process 8 allocated at: " << addr8 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr9 = alloc_for_process(9, 2048);
    std::cout << "Process 9 allocated at: " << addr9 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr10 = alloc_for_process(10, 2048);
    std::cout << "Process 10 allocated at: " << addr10 / PAGE_SIZE << " (page number)" << std::endl;
    print_memory_usage();

    v_address addr11 = alloc_for_process(11, 1024);
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
    //sendMemoryStatusToUI();

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
    v_address addr = alloc_for_process(pid, 2048);
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
void test_filesystem1() {
    std::cout << "\n=== Testing File System ===" << std::endl;
    //FileSystem fs(256, 4096);

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
    // 创建正好占满一个块的文件
    fs.createFile("/", "4kfile", FILE_TYPE, 1024);
    string fullBlock(4096, 'C');
    int writeResult = fs.writeFile("/", "4kfile", fullBlock);
    std::cout << "Full block write: " 
         << (writeResult == 0 ? "Success" : "Failed") 
         << std::endl;

    // 测试4：大文件测试
    std::cout << "\n[Test 4] Large file test:" << std::endl;
    const int LARGE_SIZE = 8 * 1024;  // 8个块
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
    FileSystem fs(10, 1024);

    // 创建目录
    fs.createDirectory("/", "documents");
    fs.createDirectory("/", "121");
    fs.createDirectory("/121", "d");
    fs.printDirectory("/"); // 应显示空目录

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
    // 最终验证前尝试删除所有子项
    std::cout << "\n[Cleanup] 清理根目录下的所有子目录..." << std::endl;
    std::vector<std::string> rootEntries = fs.listDirectory("/");
    for (const auto& entry : rootEntries) {
        if (entry.back() == '/') { // 如果是目录
            std::string dirName = entry.substr(0, entry.size() - 1); // 去掉末尾 '/'
            fs.deleteDirectoryRecursive("/" + dirName);
        } else {
            fs.deleteFile("/", entry);
        }
    }

    // 再次打印根目录确认为空
    std::cout << "\n[INFO] 删除完成后根目录内容:" << std::endl;
    fs.printDirectory("/"); 
}

void test_filesystem2() {
    std::cout << "\n=== Testing File System ===" << std::endl;

    std::cout << "[INFO] DISK_SIZE = " << DISK_SIZE << " bytes\n";
    std::cout << "[INFO] sizeof(disk) = " << sizeof(disk) << " bytes\n";

    FileSystem fs(256, 4096);

    std::cout << "[INFO] Created file system with "
              << fs.getTotalBlocks() << " blocks of "
              << fs.getBlockSize() << " bytes each.\n";

    // 测试1：基础读写测试
    std::cout << "\n[Test 1] Basic read/write:" << std::endl;
    int createResult = fs.createFile("/", "data.bin", FILE_TYPE, 1024);
    std::cout << "createFile 返回码: " << createResult << std::endl;
    if (createResult != 0) {
        std::cerr << "文件创建失败，错误码: " << createResult << std::endl;
        return;
    }

    string testData(1024, 'A');
    std::cout << "writeFile(\"/\", \"data.bin\") ... ";
    int writeResult = fs.writeFile("/", "data.bin", testData);
    std::cout << (writeResult == 0 ? "Success\n" : "Failed\n");

    string content = fs.readFile("/", "data.bin");
    std::cout << "Read data.bin: " << content.substr(0, 50) << "..." << std::endl;
}

void test_filesystem_with_memory() {
    std::cout << "\n=== Testing File System and Memory Collaboration ===" << std::endl;

    // 初始化文件系统和内存管理
    init_memory();  // 初始化内存模块
    FileSystem fs(10, 1024);  // 初始化文件系统（256块，每块4096字节）

    // Step 1: 创建目录结构
    std::cout << "[Step 1] Creating directory structure..." << std::endl;
    fs.createDirectory("/", "home");
    fs.createDirectory("/home", "user");
    fs.createDirectory("/home/user", "docs");

    fs.printDirectory("/");
    fs.printDirectory("/home/user");

    // Step 2: 创建并写入文件
    std::cout << "\n[Step 2] Creating and writing to file..." << std::endl;
    std::string content = "This is a sample text stored in memory and disk.";
    fs.createFile("/home/user/docs", "sample.txt", FILE_TYPE, content.size());
    fs.writeFile("/home/user/docs", "sample.txt", content);

    // Step 3: 读取文件并验证
    std::cout << "\n[Step 3] Reading file from disk..." << std::endl;
    std::string readContent = fs.readFile("/home/user/docs", "sample.txt");
    std::cout << "Read content: " << readContent << std::endl;

    if (readContent == content) {
        std::cout << "[PASS] File content verified successfully." << std::endl;
    } else {
        std::cerr << "[FAIL] File content mismatch!" << std::endl;
    }

    // Step 4: 将文件加载到内存中模拟程序使用
    std::cout << "\n[Step 4] Loading file into memory for processing..." << std::endl;
    m_pid pid = 1;
    v_address mem_addr;

    if (alloc_for_file(content.size(), &mem_addr, pid) != 0) { // 使用 pid=1
        std::cerr << "[ERROR] Failed to allocate memory for file." << std::endl;
        return;
    }
    std::cout << "Allocated virtual address: 0x" << std::hex << mem_addr << std::dec << std::endl;

    // Step 5: 将文件内容复制到内存
    p_address phys_addr;
    int result = translate_address(mem_addr, pid, &phys_addr); // 这里使用相同 pid=1
    if (result != 0) {
        std::cerr << "[ERROR] Address translation failed." << std::endl;
        return;
    }

    std::cout << "[DEBUG] Physical address: 0x" << std::hex << phys_addr << std::dec << std::endl;
    memcpy(&memory[phys_addr], readContent.data(), readContent.size());

    // Step 6: 从内存读回数据验证一致性
    std::string processedData;
    processedData.resize(readContent.size());
    memcpy(&processedData[0], &memory[phys_addr], readContent.size());

    std::cout << "Processed data from memory: " << processedData << std::endl;
    if (processedData == content) {
        std::cout << "[PASS] Data loaded into memory correctly." << std::endl;
    } else {
        std::cerr << "[FAIL] Memory data does not match original content." << std::endl;
    }

    // Step 7: 页面置换测试
    std::cout << "\n[Step 5] Testing page replacement..." << std::endl;
    v_address another_addr = alloc_for_process(2, 2048);
    std::cout << "Another allocation at: 0x" << std::hex << another_addr << std::dec << std::endl;
    print_memory_usage();

    // Step 8: 清理资源
    std::cout << "\n[Cleanup] Releasing all resources..." << std::endl;
    free_file_memory(mem_addr);
    fs.deleteFile("/home/user/docs", "sample.txt");
    fs.deleteDirectoryRecursive("/home");

    std::cout << "[INFO] Final root directory after cleanup:" << std::endl;
    fs.printDirectory("/");
}

/*int main() {
    SetConsoleOutputCP(CP_UTF8);  // 设置控制台输出为 UTF-8 编码
    //test_memory1();
    //test_memory2();
    //test_address_translation();
    //test_memory_swap();
    //test_filesystem1();
    //test_filesystem2();
    //test_directory_operations();
    test_filesystem_with_memory();
    return 0;
}*/

/*int main() {*/