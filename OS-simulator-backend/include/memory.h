#ifndef MEMORY_H
#define MEMORY_H

#include <iostream>
#include <cstring>
#include <vector>
#include "AIGCJson.hpp"

typedef unsigned int m_pid;       // 进程 ID
typedef unsigned int m_size;      // 内存大小
typedef unsigned char atom_data;  // 单字节数据类型
typedef unsigned int v_address;   // 虚拟地址
typedef uintptr_t p_address;   // 物理地址
typedef unsigned int page;        // 页面号

static_assert(sizeof(uintptr_t) == sizeof(void*), "uintptr_t大小与指针不一致");

#define PAGE_TABLE_SIZE 128       // 页表最大项数
#define V_PAGE_USE_SIZE 20        // 虚拟页数量
#define PAGE_SIZE 4096            // 单个页面大小（4KB）
#define P_PAGE_USE_SIZE 9         // 物理页数量
#define USE_RECORD_SIZE 16        // 内存使用记录项数
#define FULL (1 << 24) - 1        // 表示未分配状态的标志
#define MEMORY_SIZE (P_PAGE_USE_SIZE * PAGE_SIZE) // 物理内存大小
#define page_bit unsigned char    // 页标志位
#define DISK_SIZE (1024 * 1024)   // 模拟磁盘大小
#define DEVICE_BUFFER_START (MEMORY_SIZE - 0) // 设备缓冲区起始地址 待定
#define DEVICE_BUFFER_SIZE 0   // 设备缓冲区大小 待定

//页表项
struct PageTableItem {
    v_address v_id;     // 虚拟页号
    p_address p_id;     // 物理页号（FULL 代表未分配）
    m_pid owner;
    bool in_memory;     // 是否在内存中
    bool used;          // 访问位（Clock 置换算法使用）
    PageTableItem() : v_id(0), p_id(FULL), in_memory(false), used(false) {}
};

//物理页帧   Clock 置换
struct Frame {
    page p_id;       // 物理页号
    page v_id;       // 关联的虚拟页号
    m_pid owner;     // 所属进程
    bool used;       // Clock置换
    Frame* next;     // 指向下一个页面，形成环形链表
};

//设备管理 暂定
struct Device {
    int device_id;          // 设备 ID
    v_address buffer_address; // 设备的缓冲区地址
};

//内存管理数据
extern PageTableItem page_table[PAGE_TABLE_SIZE]; // 页表
extern page_bit v_page[V_PAGE_USE_SIZE];         // 虚拟页面使用情况 1已分配 0未分配
extern page_bit p_page[P_PAGE_USE_SIZE];         // 物理页面使用情况
extern Frame* clock_hand;                        // Clock 置换算法的指针
extern atom_data memory[MEMORY_SIZE];            // 物理内存
extern atom_data disk[DISK_SIZE];         

class MemoryOverview {
public:
    size_t page_size;            // 页面大小（4KB）
    size_t total_physical_mem;   // 物理内存总大小（字节）
    size_t used_physical_mem;    // 已使用物理内存（字节）
    size_t free_physical_mem;    // 可用物理内存（字节）
    size_t swap_total;           // 交换区总大小（字节）
    size_t swap_used;            // 已使用交换区大小（字节）
    size_t page_replacement_count; // 页面置换次数

    AIGC_JSON_HELPER(page_size, total_physical_mem, used_physical_mem,
                      free_physical_mem, swap_total, swap_used,
                      page_replacement_count)
};

class MemoryPageInfo {
public:
    int total_physical_pages;     // 物理页面数
    int used_physical_pages;      // 已使用物理页面数
    int total_virtual_pages;      // 虚拟页面数

    AIGC_JSON_HELPER(total_physical_pages, used_physical_pages, total_virtual_pages)
};

class PageReplacementInfo {
public:
    int current_clock_hand;       // 当前时钟指针指向的物理页号
    int last_page_in;             // 最近换入页面号
    int last_page_out;            // 最近换出页面号
    size_t replacement_count;     // 页面置换次数

    AIGC_JSON_HELPER(current_clock_hand, last_page_in, last_page_out, replacement_count)
};

class ProcessMemoryMappingItem {
public:
    m_pid pid;                    // 进程 ID
    std::string v_address_range;  // 虚拟地址范围（如 "0x1000-0x2000"）
    std::string p_address_range;  // 物理地址范围（如 "0x5000-0x6000"）
    int page_count;               // 页面数量
    std::string status;           // 状态：in_memory / swapped_out

    AIGC_JSON_HELPER(pid, v_address_range, p_address_range, page_count, status)
};

class MemoryStatusForUI {
public:
    MemoryOverview overview;
    MemoryPageInfo page_info;
    PageReplacementInfo replacement_info;
    std::vector<ProcessMemoryMappingItem> process_mappings;

    AIGC_JSON_HELPER(overview, page_info, replacement_info, process_mappings)
};

void fillMemoryStatus(MemoryStatusForUI& status);

//初始化内存管理模块
void init_memory();

//为进程分配虚拟地址空间
v_address alloc_for_process(m_pid pid, m_size size);

//释放进程的全部内存
void free_process_memory(m_pid pid);

//为设备分配内存缓冲区
v_address alloc_for_device(int device_id, m_size size);

/**
 * @brief 为文件分配内存
 * @param size 分配大小
 * @param addr 返回分配的虚拟地址
 * @return 成功返回 0，失败返回 -1
 */
int alloc_for_file(m_size size, v_address* addr,m_pid owner = 1);

/**
 * @brief 释放文件占用的内存
 * @param addr 释放的虚拟地址
 */
void free_file_memory(v_address addr,m_pid owner = 1);

/**
 * @brief 从指定地址读取数据
 * @param data 读取的目标缓冲区
 * @param address 读取的虚拟地址
 * @param pid 进程 ID
 * @return 成功返回 0，失败返回 -1（缺页）
 */
int read_memory(atom_data* data, v_address address, m_pid pid);

/**
 * @brief 向指定地址写入数据
 * @param data 要写入的数据
 * @param address 目标虚拟地址
 * @param pid 进程 ID
 * @return 成功返回 0，失败返回 -1（缺页）
 */
int write_memory(atom_data data, v_address address, m_pid pid);

/**
 * @brief 采用 Clock 置换算法选择被淘汰的页面
 * @return 返回被淘汰的物理页号
 */
page clock_replace();

/**
 * @brief 处理缺页中断，将磁盘上的页面调入内存
 * @param v_addr 需要调入的虚拟地址
 * @param pid 进程 ID
 * @return 成功返回 0，失败返回 -1
 */
int page_in(v_address v_addr, m_pid pid);

/**
 * @brief 将物理内存页面换出到磁盘
 * @param p_addr 需要换出的物理地址
 * @param pid 进程 ID
 * @return 成功返回 0，失败返回 -1
 */
int page_out(p_address p_addr, m_pid pid);

/**
 * @brief 处理缺页中断
 * @param pid 进程ID
 * @param v_addr 虚拟地址
 * @param info 缺页信息
 * @param data 返回数据（可选）
 * @param flag 标志位
 */
//void Pagefault(int pid, int v_addr, std::string info, int* data, int flag);

/**
 * @brief 地址转换，将虚拟地址转换为物理地址
 * @param v_addr 虚拟地址
 * @param pid 进程ID
 * @param p_addr 返回的物理地址
 * @return 成功返回0，失败返回错误码
 */
int translate_address(v_address v_addr, m_pid pid, p_address* p_addr);

int read_instruction(char* instruction_buffer, size_t max_size, v_address v_addr, m_pid pid, size_t* bytes_read);

/**
 * @brief 打印内存使用情况
 */
void print_memory_usage();



//测试函数
void test_memory();
void test_filesystem();
void test_directory_operations();

#endif