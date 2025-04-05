#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#include "../include/socket.h"

char recv_buf[1024] = { '0' };
char send_buf[1024] = { '0' };

MySocket::MySocket()
{
	this->Init();
	this->Bind();
	this->Listen();
	
	
}

void MySocket::Init()
{
	WSAStartup(MAKEWORD(2, 2), &this->wsd);

	this->sServer = socket(AF_INET, SOCK_STREAM, 0);

}

//绑定
void MySocket::Bind()
{
	//设置服务器地址
	memset(&this->server_addr, 0, sizeof(server_addr)); //清零
	this->server_addr.sin_family = AF_INET;
	this->server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); //server端ip地址
	this->server_addr.sin_port = htons(6000); //监听端口
	::bind(this->sServer, (SOCKADDR*)&this->server_addr, sizeof(SOCKADDR));
}	

void MySocket::Listen()
{
	//监听
	std::cout << "等待连接" << std::endl;
	::listen(this->sServer,10);
	if (::listen(this->sServer, 5) == SOCKET_ERROR)
	{
		std::cout << "listen failed!" << std::endl;
	}
}

SOCKET MySocket::Accept()
{
	//接受客户端连接
	int len = sizeof(SOCKADDR);
	this->sClient=accept(this->sServer, (SOCKADDR*)&this->client_addr, &len);
	return this->sClient;
	
}


void MySocket::Send(char send_buf[]) {
		send(this->sClient, send_buf, 1024, 0);
		//this->sClient = NULL;
}

void MySocket::Recv(char recv_buf[]) {
		recv(this->sClient, recv_buf, 1024, 0);
		
} 

void MySocket::Close()
{
	//关闭套接字
	closesocket(this->sClient);
	closesocket(this->sServer);
	WSACleanup();
}