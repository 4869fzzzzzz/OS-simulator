// memory.cpp
#include <iostream>
#include "../include/memory.h"
#include <Windows.h>
#include "../include/filesystem.h"
#include <vector>
#include <algorithm>
#include <WinNls.h>

using namespace std;

// ---------------- ȫ�ֱ������� ----------------

// ҳ������
PageTableItem page_table[PAGE_TABLE_SIZE];
// ����ҳʹ�����λͼ
page_bit v_page[V_PAGE_USE_SIZE] = { 0 };
// ����ҳʹ�����λͼ
page_bit p_page[P_PAGE_USE_SIZE] = { 0 };
// Clockҳ���û��㷨��ָ�루ʱ��ָ�룩
Frame* clock_hand = nullptr;
// ���棨�����ڴ�ͽ�������
atom_data memory[MEMORY_SIZE + SWAP_SIZE] = { 0 };
// ���̿ռ䣨��ű�������ҳ�棩
atom_data disk[DISK_SIZE] = { 0 };

// ---------------- ������������ ----------------

// ����һ�����е�����ҳ
static p_address find_free_ppage();
// ��ʼ�������ڴ�֡����
static void setup_frame_list();
// ����ʱ��ָ��
static void update_clock_hand();
// �ͷ�ĳ��������ռ�õ�ҳ��
static void free_pt_by_pid(m_pid pid);

// ---------------- �����ܺ���ʵ�� ----------------

// ��ʼ���ڴ�ϵͳ
void init_memory() {
    memset(page_table, 0, sizeof(page_table));
    memset(v_page, 0, sizeof(v_page));
    memset(p_page, 0, sizeof(p_page));
    setup_frame_list();
}

// Ϊָ�����̷���һ�������������ڴ�
v_address alloc_for_process(m_pid pid, m_size size) {
    int pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE; // ��Ҫ��ҳ��
    int start_page = -1;
    int consecutive = 0;

    // ���������Ŀ�������ҳ
    for (int i = 0; i < V_PAGE_USE_SIZE; ++i) {
        if (!(v_page[i / 8] & (1 << (i % 8)))) { // ����ҳ
            if (++consecutive == pages_needed) {
                start_page = i - pages_needed + 1;
                break;
            }
        } else {
            consecutive = 0;
        }
    }

    if (start_page == -1) return FULL; // �޷����䣬����FULL

    // ����ѷ��������ҳ��������ҳ����
    for (int i = 0; i < pages_needed; ++i) {
        int v_page_num = start_page + i;
        v_page[v_page_num / 8] |= 1 << (v_page_num % 8);

        PageTableItem& pt = page_table[v_page_num];
        pt.v_id = v_page_num;
        pt.p_id = find_free_ppage(); // ���ҿ�������ҳ
        pt.owner = pid;
        pt.in_memory = (pt.p_id != FULL);
        pt.used = false;

        if (pt.p_id != FULL) {
            p_page[pt.p_id / 8] |= 1 << (pt.p_id % 8);
            memset(&memory[pt.p_id * PAGE_SIZE], 0, PAGE_SIZE);
        } else {
            // û������ҳ���ã���Ҫ����ҳ��
            page_in(v_page_num * PAGE_SIZE, pid);
        }
    }
    return start_page * PAGE_SIZE;
}

// �ͷ�ָ������ռ�õ����������ڴ�
void free_process_memory(m_pid pid) {
    free_pt_by_pid(pid);
}

// ������ҳ�����ڴ�
int page_in(v_address v_addr, m_pid pid) {
    page v_page_num = v_addr / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];

    // ���ҿ�������ҳ
    page p_page_num = find_free_ppage();
    if (p_page_num == FULL) {
        // �޿�������ҳʱʹ��clock�û�
        p_page_num = clock_replace();
        if (p_page_num == FULL) {
            cerr << "[ERROR] No available physical page for page in" << endl;
            return -1;
        }
        cout << "[DEBUG] Page replacement triggered: Victim page " << p_page_num << " (Virtual page " << v_page_num << ")" << endl;
    }

    // ����ҳ����
    pt.p_id = p_page_num;
    pt.in_memory = true;
    p_page[p_page_num / 8] |= 1 << (p_page_num % 8);

    // �Ӵ����лָ�ҳ�����ݵ��ڴ�
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
            // �ҵ�һ��δ��ʹ�õ�֡�������û�
            page victim = current->p_id;
            
            // ���Ҷ�Ӧ������ҳ
            for (int i = 0; i < PAGE_TABLE_SIZE; ++i) {
                if (page_table[i].p_id == victim && page_table[i].owner == current->owner) {
                    // ���ڴ�����д�����
                    memcpy(&disk[page_table[i].v_id * PAGE_SIZE],
                           &memory[victim * PAGE_SIZE],
                           PAGE_SIZE);
                    
                    // ����ҳ����
                    page_table[i].in_memory = false;
                    page_table[i].p_id = FULL;
                    p_page[victim / 8] &= ~(1 << (victim % 8));
                    
                    clock_hand = current->next; // �ƶ�ʱ��ָ��
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

// ---------------- ��������ʵ�� ----------------

// ���ҵ�һ�����е�����ҳ
p_address find_free_ppage() {
    for (int i = 0; i < P_PAGE_USE_SIZE; ++i) {
        if (!(p_page[i / 8] & (1 << (i % 8)))) return i;
    }
    return FULL;
}

// ��ʼ������֡����ΪClock�û��㷨��׼��
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
    prev->next = clock_hand; // ѭ������
}

// �ͷ�ָ�����̵�����ҳ���ռ�õ�����ҳ
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

//�������ַ��ȡһ���ֽڵ�����
int read_memory(atom_data* data, v_address address, m_pid pid) {
    page v_page_num = address / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];
    if (!pt.in_memory) return -1; // ҳ�����ڴ���
    *data = memory[pt.p_id * PAGE_SIZE + (address % PAGE_SIZE)];
    pt.used = true; // ���Ϊ���ʹ�
    return 0;
}

// ��һ���ֽ�д�뵽ָ���������ַ
int write_memory(atom_data data, v_address address, m_pid pid) {
    page v_page_num = address / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];
    if (!pt.in_memory) return -1; // ҳ�����ڴ���
    memory[pt.p_id * PAGE_SIZE + (address % PAGE_SIZE)] = data;
    pt.used = true; // ���Ϊ���ʹ�
    return 0;
}

// Ϊ�豸�����ڴ滺������������
v_address alloc_for_device(int device_id, m_size size) {
    int pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    int start_page = -1;
    int consecutive = 0;

    // ���豸��������Χ�ڲ�����������ҳ
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

    // ����ѷ��������ҳ
    for (int i = 0; i < pages_needed; ++i) {
        int v_page_num = start_page + i;
        v_page[v_page_num / 8] |= 1 << (v_page_num % 8);

        PageTableItem& pt = page_table[v_page_num];
        pt.v_id = v_page_num;
        pt.p_id = find_free_ppage();
        pt.owner = FULL; // �豸�ڴ治�����κν���
        pt.in_memory = (pt.p_id != FULL);
        pt.used = false;

        if (pt.p_id != FULL) {
            p_page[pt.p_id / 8] |= 1 << (pt.p_id % 8);
            memset(&memory[pt.p_id * PAGE_SIZE], 0, PAGE_SIZE);
        }
    }
    return start_page * PAGE_SIZE;
}

//Ϊ�ļ������ڴ�
int alloc_for_file(m_size size, v_address* addr) {
    v_address allocated = alloc_for_process(FULL, size);
    if (allocated == FULL) return -1;
    *addr = allocated;
    return 0;
}

//�ͷ��ļ�ռ�õ��ڴ�
void free_file_memory(v_address addr) {
    page v_page_num = addr / PAGE_SIZE;
    PageTableItem& pt = page_table[v_page_num];
    
    if (pt.owner == FULL) { // ȷ�����ļ��ڴ�
        v_page[v_page_num / 8] &= ~(1 << (v_page_num % 8));
        if (pt.in_memory) {
            p_page[pt.p_id / 8] &= ~(1 << (pt.p_id % 8));
        }
        memset(&pt, 0, sizeof(PageTableItem));
    }
}

// �������ڴ�ҳ�滻��������(���ô�ɾ)
int page_out(p_address p_addr, m_pid pid) {
    // ���Ҷ�Ӧ������ҳ
    for (int i = 0; i < PAGE_TABLE_SIZE; ++i) {
        if (page_table[i].p_id == p_addr && page_table[i].owner == pid) {
            // ���ڴ�����д�����
            memcpy(&disk[page_table[i].v_id * PAGE_SIZE],
                   &memory[p_addr * PAGE_SIZE],
                   PAGE_SIZE);
            
            // ����ҳ����
            page_table[i].in_memory = false;
            page_table[i].p_id = FULL;
            p_page[p_addr / 8] &= ~(1 << (p_addr % 8));
            return 0;
        }
    }
    return -1; // δ�ҵ���Ӧҳ
}

void test_memory() {
    cout << "=== Testing Memory Management ===" << endl;
    init_memory();

    // ����1�������ڴ�������ͷ�
    cout << "\n[Test 1] Basic allocation and free:" << endl;
    m_pid pid1 = 1;
    v_address addr1 = alloc_for_process(pid1, 8192);
    cout << "Process 1 allocated at: " << addr1 << endl;
    free_process_memory(pid1);
    cout << "Freed process 1 memory" << endl;

    // ����2��������ڴ����
    cout << "\n[Test 2] Multi-process allocation:" << endl;
    m_pid pid2 = 2;
    v_address addr2 = alloc_for_process(pid2, 16384);
    cout << "Process 2 allocated at: " << addr2 << endl;
    m_pid pid3 = 3;
    v_address addr3 = alloc_for_process(pid3, 32768);
    cout << "Process 3 allocated at: " << addr3 << endl;

    // ����3���ڴ��д����
    cout << "\n[Test 3] Memory read/write operations:" << endl;
    atom_data data;
    write_memory(0xAA, addr2, pid2);
    read_memory(&data, addr2, pid2);
    cout << "Read from process 2: " << (int)data << endl;

    // ����4���豸�ڴ����
    cout << "\n[Test 4] Device memory allocation:" << endl;
    v_address dev_addr = alloc_for_device(1, 4096);
    cout << "Device buffer allocated at: " << dev_addr << endl;

    // ����5���ļ��ڴ�������ͷ�
    cout << "\n[Test 5] File memory allocation:" << endl;
    v_address file_addr;
    if (alloc_for_file(8192, &file_addr) == 0) {
        cout << "File memory allocated at: " << file_addr << endl;
        free_file_memory(file_addr);
        cout << "Freed file memory" << endl;
    }

    // ����6��ҳ���û�
    cout << "\n[Test 6] Page replacement:" << endl;
    // ��������ڴ��Դ���ҳ���û�
    for (int i = 0; i < 100; i++) {
        alloc_for_process(4 + i, 65536);
    }
    cout << "Page replacement triggered" << endl;

    // ����7���ڴ�״̬����
    cout << "\n[Test 7] Memory status report:" << endl;
    sendMemoryStatusToUI();

    // ����
    free_process_memory(pid2);
    free_process_memory(pid3);
    for (int i = 0; i < 100; i++) {
        free_process_memory(4 + i);
    }
}

void test_filesystem() {
    cout << "\n=== Testing File System ===" << endl;
    FileSystem fs(1024, 4096);

    // ����1��������д����
    cout << "\n[Test 1] Basic read/write:" << endl;
    fs.createFile("/", "data.bin", FILE_TYPE, 1024);
    
    // д�뾫ȷ��С������
    string testData(1024, 'A');  // 1024�ֽ�ȫ'A'
    fs.writeFile("/", "data.bin", testData);
    // ��ȡ��֤
    string content = fs.readFile("/", "data.bin");
    cout << "File content: " << content << endl;
    cout << "Data verification: " 
         << (content == testData ? "Passed" : "Failed") 
         << endl;

    // ����2������д�����
    cout << "\n[Test 2] Overwrite test:" << endl;
    string newData(512, 'B');
    fs.writeFile("/", "data.bin", newData);
    
    content = fs.readFile("/", "data.bin");
    cout << "Overwrite result: "
         << (content.substr(0,512) == string(512, 'B') && 
             content.substr(512) == string(512, 'A') ? "Passed" : "Failed")
         << endl;
    
    // ���data.bin����
    cout << "File content: " << content << endl;

    // ����3���߽���������
    cout << "\n[Test 3] Boundary condition test:" << endl;
    // ��������ռ��һ������ļ���4096�ֽڣ�
    fs.createFile("/", "4kfile", FILE_TYPE, 4096);
    string fullBlock(4096, 'C');
    int writeResult = fs.writeFile("/", "4kfile", fullBlock);
    cout << "Full block write: " 
         << (writeResult == 0 ? "Success" : "Failed") 
         << endl;

    // ����4�����ļ�����
    cout << "\n[Test 4] Large file test:" << endl;
    const int LARGE_SIZE = 16 * 4096;  // 16����
    fs.createFile("/", "large.bin", FILE_TYPE, LARGE_SIZE);
    
    string largeData(LARGE_SIZE, 'D');
    writeResult = fs.writeFile("/", "large.bin", largeData);
    cout << "Large file write: "
         << (writeResult == 0 ? "Success" : "Failed") 
         << " (" << LARGE_SIZE << " bytes)" 
         << endl;

    // ����5���ռ������֤
    cout << "\n[Test 5] Space reclamation:" << endl;
    fs.deleteFile("/", "data.bin");
    fs.deleteFile("/", "4kfile");
    cout << "Deleted two files" << endl;
    
    // ��֤�ռ��Ƿ����
    cout << "Final free space list:" << endl;
    fs.printFreeSpaceList();
}

void test_directory_operations() {
    FileSystem fs(1024, 4096);

    // ����Ŀ¼
    fs.createDirectory("/", "documents");
    fs.printDirectory("/documents"); // Ӧ��ʾ��Ŀ¼

    // �����ļ�����һ�Σ�
    int createResult = fs.createFile("/documents", "notes.txt", FILE_TYPE, 1024);
    if (createResult == 0) {
        fs.printDirectory("/documents");
    } else {
        cerr << "�ļ�����ʧ�ܣ�������: " << createResult << endl;
        return; // ��ǰ��ֹ���Ų�����
    }

    // ������Ŀ¼
    fs.createDirectory("/documents", "projects");
    fs.createDirectory("/documents", "1111");
    fs.printDirectory("/documents"); // Ӧ��ʾ�ļ���Ŀ¼

    // д���ļ�����
    fs.writeFile("/documents", "notes.txt", "Project meeting notes...");
    int writeResult = fs.writeFile("/documents", "notes.txt", "Project meeting notes...");
    if (writeResult == 0) {
        cout << "\n�ļ�д��ɹ�";
        
        // ��ȡ��չʾ����
        cout << "\n�ļ�����Ԥ����";
        string content = fs.readFile("/documents", "notes.txt");
        cout << "\n--------------------------------------------------";
        cout << "\n" << content.substr(0, 100) << "..." << endl; // ��ʾǰ100�ַ�
        cout << "--------------------------------------------------\n";
    } else {
        cout << "\n�ļ�д��ʧ��";
    }
    
    // �ݹ�ɾ��
    cout << "\nDeleting /documents recursively..." << endl;
    
    //ɾ����Ŀ¼
    fs.deleteDirectoryRecursive("/documents/projects");
    fs.printDirectory("/documents");
    
    // ������֤
    fs.printDirectory("/");
}

//�����ڴ�״̬��UI
int sendMemoryStatusToUI() {
    //�����UIͨ�ŵĴ���
    //���磺ͳ�Ʋ������ڴ�ʹ�������ҳ���û���������Ϣ
    return 0;
}

/*int main() {
    SetConsoleOutputCP(CP_UTF8);  // ���ÿ���̨���Ϊ UTF-8 ����
    test_memory();
    //test_filesystem();
    //test_directory_operations();
    
    return 0;
}*/