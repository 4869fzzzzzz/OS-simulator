#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#include "../include/socket.h"
#include "../include/interrupt.h"

char recv_buf[1024] = { '0' };
char send_buf[1024] = { '0' };
SOCKET clientSocket;

MySocket::MySocket()
{
	this->Init();
	tv.tv_sec = 1;
    tv.tv_usec = 0;
	this->Bind();
	this->Listen();
	
	
}

void MySocket::Init()
{
	WSAStartup(MAKEWORD(2, 2), &this->wsd);
	acceptflag=false;
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
	clientSocket=this->sClient;
	acceptflag=true;
	return this->sClient;
	
}

void MySocket::setNonBlocking(SOCKET sock) {
	u_long mode = 1;  // 1表示非阻塞，0表示阻塞
	ioctlsocket(sock, FIONBIO, &mode);
}

void MySocket::Send(char send_buf[]) {
		send(this->sClient, send_buf, 1024, 0);
		//this->sClient = NULL;
}

void MySocket::Recv(char recv_buf[]) {
		recv(this->sClient, recv_buf, 1024, 0);
		
}

void MySocket::HandleConnection() {
    FD_ZERO(&this->readfds);
    FD_SET(this->sServer, &this->readfds);
    if(this->sClient != INVALID_SOCKET) {
        FD_SET(this->sClient, &readfds);
    }
    
    int activity = select(0, &readfds, NULL, NULL, &tv);
    if (activity < 0) {
        std::cerr << "select error: " << WSAGetLastError() << std::endl;
        return;
    }
    
    if (activity > 0) {
        // 处理新连接
        if(FD_ISSET(this->sServer, &readfds)) {
            if(this->sClient == INVALID_SOCKET) {
                if (this->Accept() == INVALID_SOCKET) {
                    std::cerr << "Accept failed" << std::endl;
                }
            }
        }
        // 处理数据接收
        if(this->sClient != INVALID_SOCKET && FD_ISSET(this->sClient, &readfds)) {
            char buffer[1024] = {0};
            int valread = recv(this->sClient, buffer, sizeof(buffer), 0);
            
            if(valread > 0) {
                std::cout << "Received: " << buffer << std::endl;
                // 处理数据
				std::string result;
				handleClientCmd(std::string(buffer), result);
				//返回数据
				result="OK:"+result;
				send(this->sClient, result.c_str(), result.size(), 0);
				std::cout << "Sent: " << result << std::endl;
				result.clear();
            } else if(valread == 0) {
                std::cout << "Client disconnected" << std::endl;
                closesocket(this->sClient);
                this->sClient = INVALID_SOCKET;
            } else {
                std::cerr << "recv failed: " << WSAGetLastError() << std::endl;
            }
        }
    }
}
void MySocket::Close()
{
	//关闭套接字
	closesocket(this->sClient);
	closesocket(this->sServer);
	WSACleanup();
}



void RecvThread(MySocket &serverSocket){
	
	clientSocket = serverSocket.Accept();
    SOCKADDR_IN client_addr = serverSocket.client_addr;
	while(1){
		char buffer[1024] = {0};
		int valread = recv(clientSocket, buffer, sizeof(buffer), 0);
		
		if(valread > 0) {
			std::cout << "Received: " << buffer << std::endl;
			// 处理数据
			std::string result;
			handleClientCmd(std::string(buffer), result);
			//返回数据
			result="OK:"+result;
			send(clientSocket, result.c_str(), result.size(), 0);
			std::cout << "Sent: " << result << std::endl;
			result.clear();
		} else if(valread == 0) {
			std::cout << "Client disconnected" << std::endl;
			closesocket(clientSocket);
			exit(0);
			clientSocket = INVALID_SOCKET;
		} else {
			std::cerr << "recv failed: " << WSAGetLastError() << std::endl;
		}
	}
}