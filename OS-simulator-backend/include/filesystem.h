#pragma once
#include <iostream>
#include <vector>
#include <unordered_map>
#include <ctime>
#include "memory.h"

using namespace std;

// �ļ�Ȩ�޶���
#define READ "r"      // ֻ��Ȩ��
#define WRITE "w"     // дȨ��
#define EXECUTE "x"   // ִ��Ȩ��

// �ļ����Ͷ���
#define FILE_TYPE 0      // ��ͨ�ļ�
#define DIRECTORY_TYPE 1 // Ŀ¼

//FCB
struct FCB {
    string filename;        // �ļ���
    int fileType;           // �ļ����ͣ���ͨ�ļ�/Ŀ¼��
    int size;               // �ļ���С
    p_address diskAddress;  // �洢���ļ��Ĵ��̵�ַ
    time_t creationTime;    // �ļ�����ʱ��
    time_t lastModifiedTime;// �ļ�����޸�ʱ��
    string permissions;     // �ļ�Ȩ�ޣ�rwx��
};

// Ŀ¼��  �洢Ŀ¼�е��ļ�����Ŀ¼
struct DirEntry {
    string name; // �ļ���Ŀ¼����
    FCB* fcb;   // ָ���ļ����ƿ�
};

// Ŀ¼�ṹ  ������֯�ļ�����Ŀ¼�Ĳ�νṹ
struct Directory {
    string name;               // Ŀ¼����
    vector<DirEntry> entries;  // Ŀ¼�µ������ļ�����Ŀ¼
    Directory* parent;         // ָ��Ŀ¼
};

// �ļ�ϵͳ��
class FileSystem {
private:
    Directory* rootDir;                     // ��Ŀ¼
    unordered_map<string, FCB*> fileTable;  // �ļ�·����FCB��ӳ�䣨���ٲ��ң�
    int totalBlocks;                        // �ļ�ϵͳ�ܿ���
    int blockSize;                          // ÿ����Ĵ�С
    vector<bool> diskUsage;                 // ��¼���̿��ʹ�����

    // ������̿ռ�
    p_address allocateDiskSpace(int size);
    // �ͷŴ��̿ռ�
    void freeDiskSpace(p_address address, int size);
    Directory* findDirectory(const string& path);
    std::string format_time(time_t rawtime);
public:
    FileSystem(int totalBlocks, int blockSize); // ���캯��
    ~FileSystem();

    // �ļ�������
    int createFile(string path, string filename, int fileType, int size); // �����ļ�
    int deleteFile(string path, string filename); // ɾ���ļ�

    // Ŀ¼������
    int createDirectory(const string& path, const string& dirname);
    int deleteDirectory(const string& path);
    vector<string> listDirectory(const string& path);
    int deleteDirectoryRecursive(const string& path);

    // �ļ���д����
    string readFile(string path, string filename); // ��ȡ�ļ�����
    int writeFile(string path, string filename, string data); // д���ļ�

    // �ļ�Ȩ�޹���
    int setPermissions(string path, string filename, string permissions); // �����ļ�Ȩ��

    // �ļ�ϵͳ��Ϣ
    void printFileSystemStructure(); // ��ӡ�ļ�ϵͳ�Ĳ�νṹ
    void printDirectory(const string& path);
    void printFreeSpaceList(); // ��ӡ���пռ���Ϣ
    void printUtilizationRate(); // ��ӡ���ʹ����
};