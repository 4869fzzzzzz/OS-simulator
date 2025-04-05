#pragma once 
#include "./headfile.h"

#pragma comment(lib,"ws2_32.lib")     //链接库文件
#define MAXSIZE 1024
#define IP_ADDR "127.0.0.1"
#define IP_PORT 6000

extern char recv_buf[1024];
extern char send_buf[1024];

class MySocket 
{
public:
	WSADATA wsd;         //定义WSADATA对象
	SOCKET sServer;      //定义SOCKET对象
	SOCKET sClient;      //定义SOCKET对象
	SOCKADDR_IN server_addr, client_addr; //定义sockaddr_in对象
	int clientnum = 0;
	
	
	/*thread thread_send;
	thread thread_recv;*/
	char send_buf[MAXSIZE] = {'0'};   //定义发送缓冲区
	char recv_buf[MAXSIZE] = { '0'};   //定义接收缓冲区
	MySocket();
	void Init();
	void Bind();
	void Listen();
	SOCKET Accept();
	void Send(char send_buf[]);
	void Recv(char recv_buf[]);
	//void ThreadInit();
	
	void Close();

};