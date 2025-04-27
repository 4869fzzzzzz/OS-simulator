#include <windows.h>
#include <iostream>
#include <filesystem>
#include "../include/process.h"
#include "../include/filesystem.h"
#include "../include/memory.h"

#define USERMODE 0
#define KERNELMODE 1

#define SCHED_FCFS 0              
#define SCHED_RR 1                
#define SCHED_PRO 2               
#define SCHED_RRP 3

#define IN 0
#define OUT 1

using namespace std;
int pidSum;//当前进程总数
int Timer;
CPU cpu;
FileSystem fs(100,1024);
bool CPUbusy;
bool KeyBoardBusy;
bool printBusy;
int blocks = 10; //内存块数
int policy;

list<PCB> PCBList;//总进程队列
list<PCB> readyList;//就绪队列
list<PCB> blockList;//阻塞队列
list<PCB> suspendList;//挂起队列

//阻塞状态队列
list<PCB> waitForKeyBoardList;//等待键盘队列
list<PCB> waitForPrintList;//等待打印机队列
map<string, struct mutexInfo>fileMutex;

void init() {
	pidSum = 0;
	Timer = 0;
	CPUbusy = false;
	KeyBoardBusy = false;
	printBusy = false;

	cpu.pid = -1;
	cpu.PC = 0;

	//此处需要补齐创建所有文件的互斥锁
	vector<string>fileExist;
	fileExist = fs.getAllFilePaths("/documents");
	for (int i = 0; i < fileExist.size(); i++) {
		fileMutex[fileExist[i]].isBusy = false;//这个步骤的目的是为了实现 所有文件初始化都没有人访问即临界区false
	}

	init_memory();

};

PCB create(int blocks, int prority, int total_time, list<string> temp) {
	PCB p;
	p.uid = (short)1;
	p.pid = pidSum + 1;
	p.state = CREATING;
	p.cpuState = USERMODE;
	p.prority = prority;

	p.alltime = total_time; 
	p.cputime = 0;
	p.cpuStartTime = -1;
	p.keyboardStartTime = -1;
	p.printStartTime = -1;
	p.filewriteStartTime = -1;
	p.createtime = Timer;
	p.RR = (Timer - p.createtime)/p.cputime;

	p.blocktype = NOTBLOCK;
	p.task_size = blocks;
	p.is_apply = false;
	p.address = 0;
	p.program = temp;
	pidSum++;
	cout << "****创建进程pid=" << p.pid << "中****" << endl;
	return p;

};

int DeviceControl() {
	cout << "此处使用了设备";
	return 1;

}

void changePCBList(PCB& p, int state) {
	for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
		if (state == RUNNING) {
			cpu.pid = p.pid;
		}
		if (it->pid == p.pid) {
			if (state == DEAD) {
				it->program.pop_front();
				it->program.push_front("NOP");
			}
			it->state = state;
			it->cpuState = p.cpuState;
		}
	}
};

void ready(PCB& p) {  
	p.state = READY;
	readyList.push_back(p);
	changePCBList(p, READY);
	cout << "Timer=" << Timer << " 进程pid=" << p.pid << "进入就绪队列" << endl;
};
void block(PCB& p) {
	p.state = BLOCK;
	blockList.push_back(p);
	changePCBList(p, BLOCK);
	cout << "Timer=" << Timer << " 进程pid=" << p.pid << "进入阻塞队列" << endl;
};
void suspend(PCB& p) {
	p.state = SUSPEND;
	blockList.push_back(p);
	changePCBList(p, SUSPEND);
	cout << "Timer=" << Timer << " 进程pid=" << p.pid << "进入挂起队列" << endl;
};
void stop(PCB& p) {
	cout << "Timer=" << Timer << " 进程pid=" << p.pid << "结束" << endl;
	changePCBList(p, DEAD);
	free_process_memory(p.pid);
};

string getFileNameFromPath(string path) {
	size_t lastSlash = path.find_last_of('/');
	if (lastSlash == string::npos) {
		// 如果找不到斜杠，说明整个字符串就是文件名
		return path;
	}
	else {
		// 返回斜杠后的部分
		return path.substr(lastSlash + 1);
	}
}
string parseWriteCommand(string& command) {
	// 使用 stringstream 来解析指令
	stringstream ss(command);
	string word;

	// 读取并忽略指令中的 "W"
	ss >> word;  // 这里读取到的 word 应该是 "W"

	// 读取文件路径
	string filePath;
	ss >> filePath;  // 读取文件路径（假设路径没有空格）

	// 剩下的部分是写入内容
	string content;
	getline(ss, content);  // 读取文件路径后的内容作为写入内容
	if (!content.empty() && content[0] == ' ') {
		content.erase(0, 1);  // 移除开头的空格
	}
	return content;
}

void waitForFile(string filePath, string fileName) {
	cout << "filePath" << filePath;
	if (fileMutex[filePath].isBusy == false) {//如果当前的临界区没有进程访问
		if (!fileMutex[filePath].waitForFileList.empty()) {
			cout << "Timer=" << Timer << " 进程pid=" << fileMutex[filePath].waitForFileList.front().pid << "开始I/O文件:" << filePath << endl;
			fileMutex[filePath].isBusy = true;
			if (fileMutex[filePath].waitForFileList.front().fsState == "W") {
				fs.writeFile(filePath, fileName, fileMutex[filePath].waitForFileList.front().content);
			}
			else if (fileMutex[filePath].waitForFileList.front().fsState == "R") {
				cout << "read file" << endl;
				string s = "read file:" + fs.readFile(filePath, fileName);
				
			}
			else {}

			fileMutex[filePath].waitForFileList.front().filewriteStartTime = Timer;  //将第一个进程开始占用keyboard
			for (list<PCB>::iterator it = blockList.begin();
				it != blockList.end(); ++it) {
				if (it->pid == fileMutex[filePath].waitForFileList.front().pid) {
					it->filewriteStartTime = Timer;
				}
			}

		}
	}
}

void waitForPrint() {
	//检查当前的wait队列 看是否有进程正在等待或占用键盘
	if (DeviceControl() != -1) {
		if (!waitForPrintList.empty()) {
			DeviceControl();
			waitForPrintList.front().printStartTime = Timer;  //将第一个进程开始占用keyboard
			for (list<PCB>::iterator it = blockList.begin();
				it != blockList.end(); ++it) {
				if (it->pid == waitForPrintList.front().pid) {
					it->printStartTime = Timer;
				}
			}
		}
	}
};

void waitForKeyBoard() {
	//检查当前的wait队列 看是否有进程正在等待或占用键盘
	if (DeviceControl() != -1) {
		if (!waitForKeyBoardList.empty()) {
			cout << "Timer=" << Timer << " 进程pid=" << waitForKeyBoardList.front().pid << "开始占用键盘" << endl;
			DeviceControl();
			waitForKeyBoardList.front().keyboardStartTime = Timer;  //将第一个进程开始占用keyboard
			for (list<PCB>::iterator it = blockList.begin();
				it != blockList.end(); ++it) {
				if (it->pid == waitForKeyBoardList.front().pid) {
					it->keyboardStartTime = Timer;
				}
			}
		}
	}
};

void Keyboard_delete(PCB& p) {
	for (list<PCB>::iterator i = waitForKeyBoardList.begin(); i != waitForKeyBoardList.end();) {
		if (i->pid == p.pid) {
			waitForKeyBoardList.erase(i++);
		}
		else {
			i++;
		}
	}
	//KeyBoardBusy = false;
	DeviceControl();
};

void Print_delete(PCB& p) {
	for (list<PCB>::iterator i = waitForPrintList.begin(); i != waitForPrintList.end();) {
		if (i->pid == p.pid) {
			waitForPrintList.erase(i++);
		}
		else {
			i++;
		}
	}
	DeviceControl();
};

void suspenddelete(PCB& p) {
	for (list<PCB>::iterator i = suspendList.begin(); i != suspendList.end();) {
		if (i->pid == p.pid) {
			suspendList.erase(i++);
		}
		else {
			i++;
		}
	}
};

void readydelete(PCB& p) {
	for (list<PCB>::iterator i = readyList.begin(); i != readyList.end();) {
		if (i->pid == p.pid) {
			readyList.erase(i++);
		}
		else {
			i++;
		}
	}
};

void blockdelete(PCB& p) {
	for (list<PCB>::iterator i = blockList.begin(); i != blockList.end();) {
		if (i->pid == p.pid) {
			blockList.erase(i++);
		}
		else {
			i++;
		}
	}
};

/*进程是否处在占用键盘或等待键盘事件中*/
bool Under_Keyboard(PCB& p) {
	for (list<PCB>::iterator i = waitForKeyBoardList.begin(); i != waitForKeyBoardList.end(); ++i) {
		if (i->pid == p.pid) {
			return true;
		}
	}
	return false;
};

/*进程是否处在占用键盘或等待键盘事件中*/
bool Under_Print(PCB& p) {
	for (list<PCB>::iterator i = waitForPrintList.begin(); i != waitForPrintList.end(); ++i) {
		if (i->pid == p.pid) {
			return true;
		}
	}
	return false;
};
int alloc_for_process(int pid, int size) {
	if (blocks >= size) {
		blocks -= size;
		return 1;
	}
	else {
		return FULL;
	}
}

void free_process_memory(int pid) {
	for (list<PCB>::iterator it = PCBList.begin();it != PCBList.end(); ++it) {
		if (it->pid == pid) {
			blocks += it->task_size;
			return;
		}
	}
}

void swap_in(int pid) {
	for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
		if (it->pid == pid) {
			blocks -= it->task_size;
			break;
		}
	}
	cout << "这里进行了中期换入" << blocks;

}

void swap_out(int pid) {
	for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
		if (it->pid == pid) {
			blocks += it->task_size;
			break;
		}
	}
	cout << "这里进行了中期换出" << blocks;

}

void applyForResource(PCB& p) {
	//暂时只考虑到内存资源
	p.address = alloc_for_process(p.pid, p.task_size);
	if (p.address != FULL) {  //代表内存分配成功，进入就绪队列
		ready(p);           //将进程p由创建状态转入就绪状态
		p.is_apply = true;//成功分配内存
	}
	else {
		//此时内存分配失败，开始执行检查阻塞队列判断是否进行中期调度
		cout << "内存分配失败，进程pid=" << p.pid << "转入等待内存队列" << endl;
		suspend(p);
		if (blockList.size() > 0) {
			MidTermScheduler(OUT);  //中期调度将处于阻塞状态的任务选择部分换出释放内存空间
		}
	}
};

void pInterrupt(PCB& p, int reason) {
	p.cpuState = KERNELMODE;//中断状态下 进程处于内核模式
	//进程正常结束或遇到I/O阻塞等等进程进行切换时 需要保存上下文环境 将旧进程的状态保存在PCB中 新进程调入CPU
	//如果是正常的进程占用时间片结束
	if (reason == NORMAL_SWITCH) {
		p.cputime = -1;
		p.program.pop_front();
		cpu.pid = -1;
		CPUbusy = false;
		ready(p);
	}
	//抢占式进程切换 需要保存上一个进程已占用CPU时间(或者在总时间上减去已占用时间)
	if (reason == PREEMPTION_SWITCH) {
		for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
			if (it->pid == p.pid) {
				if (p.cputime != -1) {
					string s = it->program.front();
					int alltime = atoi(s.substr(2, s.size()).c_str());
					alltime -= it->cputime + 1;
					it->program.pop_front();
					if (alltime != 0) {
						it->program.push_front("C " + to_string(alltime));
					}
					it->cputime = -1;
				}
				it->cpuState = USERMODE;
				changePCBList(*it, READY);
				readyList.push_back(*it);
				break;
			}
		}
	}
	/*中期调度中断 保存占用设备时间等信息*/
	if (reason == MIDTERM_SWITCH_OUT) {
		cout << "Timer=" << Timer << " 进程pid=" << p.pid << "调出内存" << endl;
		swap_out(p.pid);//将进程从内存移至外存
		if (Under_Keyboard(p)) {
			Keyboard_delete(p);
			string s = p.program.front();
			cout << "pid=" << p.pid << " wTime " << p.keyboardStartTime << endl;
			if (p.keyboardStartTime != -1) {
				int keyTime = atoi(s.substr(2, s.size()).c_str());
				keyTime -= (Timer - p.keyboardStartTime);
				cout << "keyTime" << keyTime << endl;
				p.program.pop_front();
				if (keyTime != 0) {
					p.program.push_front("K " + to_string(keyTime));
				}
			}
		}
		if (Under_Print(p)) {
			Print_delete(p);
			p.blocktype = PRINT;
			string s = p.program.front();
			cout << "pid=" << p.pid << " wTime " << p.keyboardStartTime << endl;
			if (p.printStartTime != -1) {
				int printTime = atoi(s.substr(2, s.size()).c_str());
				printTime -= (Timer - p.printStartTime);
				p.program.pop_front();
				if (printTime != 0) {
					p.program.push_front("P " + to_string(printTime));
				}
			}
		}
		suspendList.push_back(p);
		changePCBList(p, SUSPEND);
		blockdelete(p);
	}
	if (reason == MIDTERM_SWITCH_IN) {
		swap_in(p.pid);//将进程从外存移至内存
		if (p.blocktype == KEYBOARD) { //键盘阻塞
			blocks -= p.task_size;
			p.is_apply = true;
			p.blocktype = NOTBLOCK;
			cout << "Timer=" << Timer << " 进程pid=" << p.pid << "调入内存" << endl;
			readyList.push_back(p);
			changePCBList(p, READY);
		}
		else if (p.blocktype == PRINT) {
			blocks -= p.task_size;
			p.is_apply = true;
			p.blocktype = NOTBLOCK;
			cout << "Timer=" << Timer << " 进程pid=" << p.pid << "调入内存" << endl;
			readyList.push_back(p);
			p.cpuState = USERMODE;
			changePCBList(p, READY);
		}
		suspenddelete(p);
	}
	/*以下内容为IO中断*/
	if (reason == KEYBOARD_SWITCH) {
		//查找中断向量表 执行中断程序 保存中断状态
		block(p);
		waitForKeyBoardList.push_back(p);
		waitForKeyBoard();  //执行键盘输入函数
	}
	if (reason == KEYBOARD_SWITCH_OVER) {
		p.cpuState = USERMODE;
		p.program.pop_front();
		ready(p);
		blockdelete(p);
		waitForKeyBoardList.pop_front();
		for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
			if (it->pid == p.pid) {
				it->program.pop_front();
			}
		}
		DeviceControl();
		waitForKeyBoard();
	}
	if (reason == PRINT_SWITCH) {
		//查找中断向量表 执行中断程序 保存中断状态
		block(p);
		waitForPrintList.push_back(p);
		waitForPrint();  //执行键盘输入函数
	}
	if (reason == PRINT_SWITCH_OVER) {
		p.cpuState = USERMODE;
		p.program.pop_front();
		ready(p);
		blockdelete(p);
		waitForPrintList.pop_front();
		for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
			if (it->pid == p.pid) {
				it->program.pop_front();
			}
		}
		DeviceControl();
		waitForPrint();
	}
	if (reason == FILE_SWITCH_READ) {
		block(p);
		fileMutex[p.fs].waitForFileList.push_back(p);
		cout << "push:" << fileMutex[p.fs].waitForFileList.back().fsState << endl;
		waitForFile(p.fs, getFileNameFromPath(p.fs));  //执行键盘输入函数
	}

	//开始写文件
	if (reason == FILE_SWITCH_WRITE) {
		block(p);
		fileMutex[p.fs].waitForFileList.push_back(p);
		cout << "push:" << fileMutex[p.fs].waitForFileList.back().fsState << endl;
		string data = parseWriteCommand(p.program.front());
		p.content = data;
		waitForFile(p.fs, getFileNameFromPath(p.fs));  //执行键盘输入函数
	}
	//结束写文件
	if (reason == FILE_SWITCH_OVER) {
		p.program.pop_front();
		PCB& p1 = p;
		ready(p);
		blockdelete(p);
		cout << p1.fs << endl;
		string ff = p1.fs;
		map<string, struct mutexInfo>::iterator it;
		for (it = fileMutex.begin(); it != fileMutex.end(); it++) {
			cout << it->first << endl;
			if (it->first == p.fs && it->second.isBusy == true) {
				it->second.waitForFileList.pop_front();
				it->second.isBusy = false;
				break;
			}
		}
		for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
			if (it->pid == p1.pid) {
				it->program.pop_front();
			}
		}
		waitForFile(ff, getFileNameFromPath(ff));
	}
};


void MidTermScheduler(int inOrOut) {
	if (inOrOut == OUT) {
		int size = 0;
		for (int i = 0; i < blockList.size(); ++i) {
			pair<int, int> pcbPair = { INT_MAX, -1 };
			for (list<PCB>::iterator it = blockList.begin(); it != blockList.end(); ++it) {
				if (it->prority < pcbPair.first && it->is_apply) {//优先调出优先级低的进程
					pcbPair.first = it->prority;
					pcbPair.second = it->pid;
				}
			}
			if (pcbPair.second != -1) {
				for (list<PCB>::iterator it = blockList.begin(); it != blockList.end(); ++it) {
					if (it->pid == pcbPair.second) {
						size += it->task_size;
						blocks += it->task_size;
						it->is_apply = false;

						Interrupt(*it, MIDTERM_SWITCH_OUT);

						break;
					}
				}
			}
			if (size > MEMORY_SIZE / 3) {
				break;
			}
		}
	}
	else if (inOrOut == IN) {
		pair<int, int> pcbPair = { INT_MIN, -1 };
		for (list<PCB>::iterator it = suspendList.begin(); it != suspendList.end(); ++it) {
			if (it->prority > pcbPair.first && it->task_size < blocks) {
				pcbPair.first = it->prority;
				pcbPair.second = it->pid;
			}
		}
		if (pcbPair.second != -1) {
			for (list<PCB>::iterator it = suspendList.begin(); it != suspendList.end(); ++it) {
				if (it->pid == pcbPair.second) {
					Interrupt(*it, MIDTERM_SWITCH_IN);
					break;
				}
			}
		}
	}
};

void updateTaskState() {

	//每经过一个节拍更新进程状态,遍历整个进程列表

	/*遍历等待键盘队列*/
	if (!waitForKeyBoardList.empty()) {
		PCB& keyP = waitForKeyBoardList.front();
		string s = keyP.program.front();
		int keyTime = atoi(s.substr(2, s.size()).c_str());
		//cout<<keyP.wtime.keyboardStartTime<<"  "<<keyTime<< endl;
		cout << "pid=" << keyP.pid << " " << "time:" << Timer - keyP.keyboardStartTime << endl;
		if ((Timer - keyP.keyboardStartTime) == keyTime) {
			Interrupt(keyP, KEYBOARD_SWITCH_OVER);
		}
	}

	/*遍历等待打印机队列*/
	if (!waitForPrintList.empty()) {
		PCB& printP = waitForPrintList.front();
		string s = printP.program.front();
		int printTime = atoi(s.substr(2, s.size()).c_str());
		//cout<<keyP.wtime.keyboardStartTime<<"  "<<keyTime<< endl;
		cout << "pid=" << printP.pid << " " << "time:" << Timer - printP.printStartTime << endl;
		if ((Timer - printP.printStartTime) == printTime) {
			Interrupt(printP, PRINT_SWITCH_OVER);
		}
	}
	/*遍历等待fileMutex的map*/
	map<string, struct mutexInfo>::iterator it;
	for (it = fileMutex.begin(); it != fileMutex.end(); it++) {
		if (it->second.isBusy == true) {
			//如果是busy状态 那么就要检查头进程是否读写完成了
			PCB& writeP = it->second.waitForFileList.front();
			string s = writeP.program.front();

			// 假设指令格式为 "W 路径 内容 写入时间"
			// 从第二个字符开始提取，跳过 'W' 和后面的空格
			string writeTime = s.substr(2); // 从索引 2 开始，直到字符串结束

			string path, content, wtime;

			// 使用 stringstream 分割字符串
			stringstream input(writeTime);
			input >> path >> content >> wtime;
			cout << wtime << endl;
			cout << "pid=" << writeP.pid << " " << "wtime:" << Timer - writeP.filewriteStartTime << endl;
			if ((Timer - writeP.filewriteStartTime) == atoi(wtime.c_str())) {
				Interrupt(writeP, FILE_SWITCH_OVER);
			}
		}


	}

	/*遍历执行中进程*/
	for (list<PCB>::iterator it = PCBList.begin(); it != PCBList.end(); ++it) {
		if (it->state == RUNNING) {
			it->cputime++;
			string s = it->program.front();
			int cputime = it->cputime;
			int alltime = atoi(s.substr(2, s.size()).c_str());
			cout << "cputime:" << cputime << endl;
			cout << "alltime" << alltime << endl;
			if (cputime == alltime) {
				/*执行上下文切换 将被切换进程的状态保存在PCB中*/
				Interrupt(*it, NORMAL_SWITCH);

			}
		}
		else if (it->state == BLOCK) {

		}
	}
	if (blocks > 5 && !suspendList.empty()) {
		MidTermScheduler(IN);
	}
};

void Execute() {
	int max = INT_MIN;
	int max_rr = INT_MIN;
	for (list<PCB>::iterator it = readyList.begin(); it != readyList.end(); ++it) {
		if (it->cputime == 0) {
			it->RR = (Timer - it->createtime + 1) / (it->cputime + 1);
		}
		else {
			it->RR = (Timer - it->createtime) / it->cputime;
		}
		
		if (it->prority > max) {
			max = it->prority;
		}
		if (it->RR > max_rr) {
			max_rr = it->RR;
		}

	}//寻找readylist中的最高优先级
	//执行就绪队列中排队的进程
	for (list<PCB>::iterator it = readyList.begin(); it != readyList.end();) {
		string s = it->program.front().substr(0, 1);
		cout << "当前指令:" << s << endl;
		if (s == "C") {  //如果指令是C，说明需要占用CPU
			if (!CPUbusy) {  //如果当前CPU中并无进程在运行,则安排firstP
				if (policy == SCHED_PRO) {
					for (list<PCB>::iterator it1 = PCBList.begin(); it1 != PCBList.end(); ++it1) {
						if (it1->state == RUNNING) {
							if (it1->prority >= max) {
								it++;
								break;
							}
							else {
								if (it->prority == max) {
									CPUScheduler(*it);
									readyList.erase(it++);
								}
								else {
									it++;
								}
							}
							break;
						}
					}
				}
				else if (policy == SCHED_RRP) {
					for (list<PCB>::iterator it1 = PCBList.begin(); it1 != PCBList.end(); ++it1) {
						if (it1->state == RUNNING) {
							it1->RR = (Timer - it1->createtime) / it1->cputime;
							if (it1->RR >= max_rr) {
								it++;
								break;
							}
							else {
								if (it->RR == max_rr) {
									CPUScheduler(*it);
									readyList.erase(it++);
								}
								else {
									it++;
								}
							}
							break;
						}
					}
				}
				else {
					CPUScheduler(*it);  //第一个是CPU计算
					readyList.erase(it++);
				}
			}
			else {
				if (policy == SCHED_RR) {
					for (list<PCB>::iterator it1 = PCBList.begin(); it1 != PCBList.end(); ++it1) {
						if (it1->state == RUNNING) {
							cout << "running" << endl;
							Interrupt(*it1, PREEMPTION_SWITCH);
							CPUScheduler(*it);
							readyList.erase(it++);
							break;
						}
					}
					break;
				}
				else {
					it++;
				}
			}
		}
		/*除了C之外的所有命令都执行中断，进入阻塞队列，进行相应的操作*/
		else if (s == "K") {  //执行键盘输入
			Interrupt(*it, KEYBOARD_SWITCH);
			readyList.erase(it++);
		}
		else if (s == "P") {  //执行打印机输出
			Interrupt(*it, PRINT_SWITCH);
			readyList.erase(it++);
		}
		else if (s == "R") {  //执行文件读取
			string filePath = it->program.front().substr(2, it->program.front().size() - 1);
			string path, rtime;
			stringstream input(filePath);
			input >> path >> rtime;
			cout << "filePath:" << path << endl;


			it->fs = path;//此时进程访问文件的路径
			it->fsState = "R";
			Interrupt(*it, FILE_SWITCH_READ);
			readyList.erase(it++);

		}
		else if (s == "W") {  //执行文件写入
			string filePath = it->program.front().substr(2, it->program.front().size() - 1);
			string path, wtime;
			cout << filePath << endl;
			stringstream input(filePath);
			input >> path >> wtime;
			cout << "filePath:" << path << endl;
			
			it->fs = path;//此时进程访问文件的路径
			it->fsState = "W";
			Interrupt(*it, FILE_SWITCH_WRITE);
			readyList.erase(it++);
			
		}
		else if (s == "Q") {  //进程正常结束退出
			stop(*it);
			readyList.erase(it++);

		}
	}
}


void LongTermScheduler(string filename) {
	// 长期调度，消费者将外存中的程序调度内存并创建进程
	ifstream fp;
	fp.open(filename.c_str(), ios::in);
	if (fp) {
		list<string> temp;
		string line;
		int blocks;  // 占用的内存块数
		int prority;
		int totalCpuTime = 0;  // 用来累加总的CPU时间

		while (getline(fp, line))  // line中不包括每行的换行符
		{
			// 处理各种在创建进程时相关的指令
			if (line[0] == 'M') {  // 如果是占用内存相关指令 则计算出需要占用的内存块数
				string s = line.substr(2, line.size());
				blocks = atoi(s.c_str());
			}
			else if (line[0] == 'Y') {  // 如果是优先级相关指令 则计算出优先级
				string s = line.substr(2, line.size());
				prority = atoi(s.c_str());
			}
			else if (line[0] == 'C') {  // 如果是CPU时间相关指令
				// 从指令中的数字部分提取出CPU时间，并累加
				string s = line.substr(2, line.size());
				int cpuTime = atoi(s.c_str());  // 获取指令后的数字
				totalCpuTime += cpuTime;  // 累加到总CPU时间
			}
			else {
				cout << line << endl;
				temp.push_back(line);  // 其他指令作为程序指令暂时存放在temp中
			}
		}


		// 调用函数来创建进程并添加到进程控制块列表
		cout << "将程序" << filename << "调入内存" << endl;
		PCB p = create(blocks, prority, totalCpuTime, temp);
		PCBList.push_back(p);
		applyForResource(p);
	}
};

void CPUScheduler(PCB& p) {
	//短期调度将从就绪队列中选择进程调度CPU中执行
	cout << "Timer=" << Timer << " 进程pid=" << p.pid << "从就绪态转入运行态" << endl;
	changePCBList(p, RUNNING);
	CPUbusy = true;
};

