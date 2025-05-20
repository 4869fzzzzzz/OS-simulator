// memory.cpp
#include <iostream>
#include "../include/memory.h"
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
    std::cout << "[DEBUG] 请求分配: " << size << " bytes -> " 
            << pages_needed << " pages" << std::endl;
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
        pt.owner = pid;
        pt.p_id = FULL;
        pt.in_memory = false;
        pt.used = false;
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
           
    // 如果是进程的代码段，需要从文件系统读取内容
    if (pt.owner != FULL) {
        // 查找进程控制块
        PCB* pcb = nullptr;
        for (auto& p : PCBList) {
            if (p.pid == pt.owner) {
                pcb = &p;
                break;
            }
        }

        if (pcb) {
            try {
                // 首先尝试从进程的instruction字段读取
                if (!pcb->instruction.empty()) {
                    size_t copy_size = std::min(pcb->instruction.size(), static_cast<size_t>(PAGE_SIZE));
                    memcpy(&memory[p_page_num * PAGE_SIZE], pcb->instruction.c_str(), copy_size);
                    std::cout << "[DEBUG] Copied " << copy_size << " bytes from process instruction to memory" << std::endl;
                }
                // 如果进程没有指令，尝试从文件系统读取
                else if (!pcb->position.empty() && !pcb->fs.empty()) {
                    string content = fs.readFile(pcb->position, pcb->fs);
                    if (!content.empty()) {
                        size_t copy_size = std::min(content.size(), static_cast<size_t>(PAGE_SIZE));
                        memcpy(&memory[p_page_num * PAGE_SIZE], content.c_str(), copy_size);
                        std::cout << "[DEBUG] Copied " << copy_size << " bytes from file " 
                                 << pcb->fs << " to memory" << std::endl;
                        // 更新进程的instruction字段
                        pcb->instruction = content;
                    } else {
                        std::cerr << "[WARNING] Empty file content for process " << pt.owner << std::endl;
                        memset(&memory[p_page_num * PAGE_SIZE], 0, PAGE_SIZE);
                    }
                } else {
                    std::cerr << "[WARNING] No content to load for process " << pt.owner << std::endl;
                    memset(&memory[p_page_num * PAGE_SIZE], 0, PAGE_SIZE);
                }
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Failed to read content for process " << pt.owner 
                         << ": " << e.what() << std::endl;
                memset(&memory[p_page_num * PAGE_SIZE], 0, PAGE_SIZE);
            }
        } else {
            std::cerr << "[ERROR] Process " << pt.owner << " not found" << std::endl;
            memset(&memory[p_page_num * PAGE_SIZE], 0, PAGE_SIZE);
        }
    }
    
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
    cout<<"虚拟页号"<<v_page_num<<endl;
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

int read_instruction(char* instruction_buffer, size_t max_size, v_address v_addr, m_pid pid, size_t* bytes_read) {
    size_t total_read = 0;
    p_address p_addr;
    int result = translate_address(v_addr, pid, &p_addr);

    if (result == -2) { // 缺页情况
        std::cout << "[INFO] Page fault at address 0x" << std::hex << v_addr << std::dec << std::endl;

        // 调用 page_in 处理缺页
        /*if (page_in(v_addr, pid) != 0) {
            std::cerr << "[ERROR] Failed to resolve page fault for address 0x"
                        << std::hex << v_addr << std::dec << std::endl;
            
            raiseInterrupt(InterruptType::PAGEFAULT, pid, v_addr, "Page fault during instruction read", nullptr, 0);
            return -1;
        }*/

        // 成功换入页面后重新翻译地址
        /*result = translate_address(v_addr, pid, &p_addr);
        if (result != 0) {
            std::cerr << "[ERROR] Address translation failed after page in for 0x"
                        << std::hex << v_addr << std::dec << std::endl;
            return -1;
        }*/
        print_memory_usage();
        return -1;
    } else if (result != 0) { // 其他错误
        std::cerr << "[ERROR] Failed to translate address 0x" << std::hex << v_addr << std::dec << std::endl;
        print_memory_usage();
        return -3;
    }
    while (total_read < max_size - 1) { // 留出一个位置给 '\0'
       
        
        
        // 从物理地址读取单个字节
        atom_data data=memory[p_addr++];
        //read_memory(&data, v_addr + total_read, pid); // 这里会自动触发缺页处理

        char c = static_cast<char>(data);

        instruction_buffer[total_read] = c;
        total_read++;
        /*
        std::cout << "[DEBUG] read_instruction() p_addr: 0x" << std::hex << p_addr << std::dec << std::endl;
        std::cout << "[DEBUG] First 16 bytes at p_addr: ";
        for (int i = 0; i < 16 && total_read + i < max_size; ++i) {
            std::cout << std::hex << static_cast<int>(memory[p_addr + i]) << " ";
        }
        std::cout << std::dec << std::endl;*/

        if (c == '\n') {
            break; // 找到换行符
        }
    }

    instruction_buffer[total_read] = '\0'; // 字符串终止符
    *bytes_read = total_read;

    // 如果读取了 0 字节且地址越界，则返回文件尾
    if (*bytes_read == 0 && v_addr >= PAGE_SIZE * V_PAGE_USE_SIZE) {
        return -2;
    }

    return 0;
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
