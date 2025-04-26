#ifndef MEMORY_H
#define MEMORY_H

#include <iostream>
#include <cstring>
#include <vector>

typedef unsigned int m_pid;       // ���� ID
typedef unsigned int m_size;      // �ڴ��С
typedef unsigned char atom_data;  // ���ֽ���������
typedef unsigned int v_address;   // �����ַ
typedef uintptr_t p_address;   // �����ַ
typedef unsigned int page;        // ҳ���

static_assert(sizeof(uintptr_t) == sizeof(void*), "uintptr_t��С��ָ�벻һ��");

#define PAGE_TABLE_SIZE 1024 * 128  // ҳ���������
#define V_PAGE_USE_SIZE 16 * 1024   // ����ҳ����
#define PAGE_SIZE 4096              // ����ҳ���С��4KB��
#define P_PAGE_USE_SIZE 4 * 1024    // ����ҳ����
#define USE_RECORD_SIZE 1024 * 16   // �ڴ�ʹ�ü�¼����
#define FULL (1 << 24) - 1          // ��ʾδ����״̬�ı�־
#define SWAP_SIZE 4 * 1024          // �����ռ��С
#define SWAP_START 1024 * 1024 * 128 // ��������ʼ��ַ
#define MEMORY_SIZE 1024 * 1024 * 132 // �����ڴ��С
#define page_bit unsigned char      // ҳ��־λ
#define DISK_SIZE 1024 * 1024 * 512 // ģ����̴�С
#define DEVICE_BUFFER_START (MEMORY_SIZE - 1024 * 16) // �豸��������ʼ��ַ ����
#define DEVICE_BUFFER_SIZE (1024 * 16) // �豸��������С ����

//ҳ����
struct PageTableItem {
    v_address v_id;     // ����ҳ��
    p_address p_id;     // ����ҳ�ţ�FULL ����δ���䣩
    m_pid owner;
    bool in_memory;     // �Ƿ����ڴ���
    bool used;          // ����λ��Clock �û��㷨ʹ�ã�
    PageTableItem() : v_id(0), p_id(FULL), in_memory(false), used(false) {}
};

//����ҳ֡   Clock �û�
struct Frame {
    page p_id;       // ����ҳ��
    page v_id;       // ����������ҳ��
    m_pid owner;     // ��������
    bool used;       // Clock�û�
    Frame* next;     // ָ����һ��ҳ�棬�γɻ�������
};

//�豸���� �ݶ�
struct Device {
    int device_id;          // �豸 ID
    v_address buffer_address; // �豸�Ļ�������ַ
};



//�ڴ��������
extern PageTableItem page_table[PAGE_TABLE_SIZE]; // ҳ��
extern page_bit v_page[V_PAGE_USE_SIZE];         // ����ҳ��ʹ����� 1�ѷ��� 0δ����
extern page_bit p_page[P_PAGE_USE_SIZE];         // ����ҳ��ʹ�����
extern Frame* clock_hand;                        // Clock �û��㷨��ָ��
extern atom_data memory[MEMORY_SIZE + SWAP_SIZE];// �����ڴ� + �����ռ�
extern atom_data disk[DISK_SIZE];                // ģ�����

//��ʼ���ڴ����ģ��
void init_memory();

//Ϊ���̷��������ַ�ռ�
v_address alloc_for_process(m_pid pid, m_size size);

//�ͷŽ��̵�ȫ���ڴ�
void free_process_memory(m_pid pid);

//Ϊ�豸�����ڴ滺����
v_address alloc_for_device(int device_id, m_size size);

/**
 * @brief Ϊ�ļ������ڴ�
 * @param size �����С
 * @param addr ���ط���������ַ
 * @return �ɹ����� 0��ʧ�ܷ��� -1
 */
int alloc_for_file(m_size size, v_address* addr);

/**
 * @brief �ͷ��ļ�ռ�õ��ڴ�
 * @param addr �ͷŵ������ַ
 */
void free_file_memory(v_address addr);

/**
 * @brief ��ָ����ַ��ȡ����
 * @param data ��ȡ��Ŀ�껺����
 * @param address ��ȡ�������ַ
 * @param pid ���� ID
 * @return �ɹ����� 0��ʧ�ܷ��� -1��ȱҳ��
 */
int read_memory(atom_data* data, v_address address, m_pid pid);

/**
 * @brief ��ָ����ַд������
 * @param data Ҫд�������
 * @param address Ŀ�������ַ
 * @param pid ���� ID
 * @return �ɹ����� 0��ʧ�ܷ��� -1��ȱҳ��
 */
int write_memory(atom_data data, v_address address, m_pid pid);

/**
 * @brief ���� Clock �û��㷨ѡ����̭��ҳ��
 * @return ���ر���̭������ҳ��
 */
page clock_replace();

/**
 * @brief ����ȱҳ�жϣ��������ϵ�ҳ������ڴ�
 * @param v_addr ��Ҫ����������ַ
 * @param pid ���� ID
 * @return �ɹ����� 0��ʧ�ܷ��� -1
 */
int page_in(v_address v_addr, m_pid pid);

/**
 * @brief �������ڴ�ҳ�滻��������
 * @param p_addr ��Ҫ�����������ַ
 * @param pid ���� ID
 * @return �ɹ����� 0��ʧ�ܷ��� -1
 */
int page_out(p_address p_addr, m_pid pid);

//���ڴ�״̬����ui
int sendMemoryStatusToUI();

//���Ժ���
void test_memory();
void test_filesystem();
void test_directory_operations();

#endif