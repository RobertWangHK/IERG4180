#include <winsock2.h>
#include <ws2tcpip.h>
#include "getopt.h"
#pragma comment(lib, "Ws2_32.lib") 

//common header files for both Windows and Linux
#include <string.h>  //for menset
#include <iostream>
#include <mutex>
#include <thread>
#include <string>
#include "util.h"
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <fstream>
#include <sstream>

#define DEFAULT_HOST_NAME "localhost"
#define DEFAULT_PORT_NUM 4181
#define SECOND_PER_MINUTE CLOCKS_PER_SEC

using std::cout;
using std::endl;
using std::string;
using std::to_string;
std::mutex mtx_thread;
//using namespace std::chrono;

string rHost = DEFAULT_HOST_NAME;
string rPort = to_string(DEFAULT_PORT_NUM);
string folderName = "None";

int send_Para(string folderName, SOCKET connect);
void handle_send(SOCKET connect);
void handle_recv(SOCKET connect);

time_t filetime_to_timet(const FILETIME& ft);

int main(int argc, char *argv[])
{
	
	WORD version = MAKEWORD(2, 2);
	WSADATA wsa_data;
	int error;

	error = WSAStartup(version, &wsa_data);

	if (error != 0)
	{
		printf("WSAStartup failed with error=%d\n", error);
		WSACleanup();
		return -1;
	}

	if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2)
	{
		printf("cannot find winsock 2.2\n");
		WSACleanup();
		return -1;
	}


	//You code here...
	//here are just some examples to bind an address to a socket 
	static struct option long_options[] =
	{
		{ "rhost", required_argument, 0, 1 },
		{ "rport", required_argument, 0, 2 },
		{ "folder", required_argument, 0, 3 },
		{ 0, 0, 0, 0 }
	};
	int c;
	while ((c = getopt_long_only(argc, argv, "", long_options, 0)) != -1)
	{
		switch (c)
		{
		case 1:
			rHost = string(optarg);
			break;
		case 2:
			rPort = string(optarg);
			break;
		case 3:
			folderName = string(optarg);
			break;
		default:
			break;
		}
	}

	//initiate the starting TCP socket for sharing parameter information with the server
	struct addrinfo aiHints;
	struct addrinfo *aiList = NULL;
	memset(&aiHints, 0, sizeof(aiHints));
	aiHints.ai_family = AF_INET;
	aiHints.ai_socktype = SOCK_STREAM;
	aiHints.ai_protocol = IPPROTO_TCP;
	
	if (getaddrinfo(rHost.c_str(), rPort.c_str(), &aiHints, &aiList) != 0)
	{
		cout << "getaddrinfo() failed. Error code: " << WSAGetLastError() << endl;
		return -1;
	}
	SOCKET peer_socket_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (connect(peer_socket_tcp, aiList->ai_addr, sizeof(struct sockaddr)) == SOCKET_ERROR)
	{
		cout << "connect() failed. Error code: " << WSAGetLastError() << endl;
		return -1;
	}
	cout << "connect to the server " << rHost << ":" << rPort << endl;

	//check if no folderName specified.
	if (folderName.compare("None") == 0) {
		cout << "please specify client folder" << endl;
		return 0;
	}
	//fetch all file and information of that folder.
	send_Para(folderName, peer_socket_tcp);
	char buffer[256];
	do {
		//recv or send mode
		memset(buffer, 0, 256);
		recv_line (peer_socket_tcp, buffer, 256, MSG_WAITALL); //terminated by \0
		string mode = string(buffer);
		//ask client to send files
		if (mode.compare("send") == 0) {
			handle_send(peer_socket_tcp);
		}
		else if (mode.compare("recv") == 0) {
			handle_recv(peer_socket_tcp);
		}
	} while (true);

	WSACleanup();
	return 0;
}

int send_Para(string para, SOCKET peer_socket_tcp) {

	//initialize for file operation
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind;
	LARGE_INTEGER filesize;
	FILETIME lastModified;
	//initialize for tcp connection
	char buffer[256];
	memset(buffer, 0, 256);
	memcpy(buffer, &para, sizeof(para));
	send_full(peer_socket_tcp, buffer, 256, 0);

	//para = string("test_folder");
	cout << para << endl;

	hFind = FindFirstFile((TCHAR *)para.c_str(), &FindFileData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		printf("FindFirstFile failed (%d)\n", GetLastError());
		return 0;
	}
	do
	{
		lastModified = FindFileData.ftLastWriteTime;
		filesize.LowPart = FindFileData.nFileSizeLow;
		filesize.HighPart = FindFileData.nFileSizeHigh;
		long int FileSize = filesize.QuadPart;
		//send file name
		memset(buffer, 0, 256);
		snprintf(buffer, 256, (char *)FindFileData.cFileName, 0);
		send_full(peer_socket_tcp, buffer, 256, 0);

		////send file size
		//memset(buffer, 0, 256);
		//snprintf(buffer, 256, (char *)(filesize.QuadPart), 0);
		//send_full(peer_socket_tcp, buffer, 256, 0);

		//send last modified date
		memset(buffer, 0, 256);
		time_t LastModifiedTime = filetime_to_timet(FindFileData.ftLastWriteTime);
		snprintf(buffer, 256, (char *)(LastModifiedTime), 0);
		send_full(peer_socket_tcp, buffer, 256, 0);

		_tprintf(TEXT("  %s   %ld bytes\n"), FindFileData.cFileName, FileSize);

	} while (FindNextFile(hFind, &FindFileData) != 0);
	memset(buffer, 0, 256);
	snprintf(buffer, 256, "end", 0);
	send_full(peer_socket_tcp, buffer, 256, 0);

	return 0;
}

void handle_send(SOCKET peer_socket_tcp) {
	char buffer[256];
	string file_path;
	std::ifstream infile;
	string file_string;
	memset(buffer, 0, 256);
	recv_line(peer_socket_tcp, buffer, 256, MSG_WAITALL);
	file_path = string(buffer);
	while (file_path.compare("end") != 0){
		//send requested file
		infile.open(file_path.c_str());
		std::stringstream file_buffer;
		file_buffer << infile.rdbuf();
		file_string = file_buffer.str();
		//send the file string to server
		char * content_buffer = new char[file_string.size() + 1];
		std::copy(file_string.begin(), file_string.end(), content_buffer);
		content_buffer[file_string.size()] = '\0';
		send_full(peer_socket_tcp, content_buffer, file_string.size() + 1, 0);
		//receive next file requests
		memset(buffer, 0, 256);
		recv_line(peer_socket_tcp, buffer, 256, MSG_WAITALL);
		file_path = string(buffer);
	}
	memset(buffer, 0, 256);
	snprintf(buffer, 256, "end", 0);
	send_full(peer_socket_tcp, buffer, 256, 0);
	return;
}

void handle_recv(SOCKET peer_socket_tcp) {
	char buffer[256];
	char * content_buffer = (char *)malloc(sizeof(char) * 1000);
	long int buffer_size = 1000;//for content_buffer length specifically
	string fileName;
	std::ofstream output;
	memset(buffer, 0, 256);
	memset(content_buffer, 0, buffer_size);
	recv_line(peer_socket_tcp, buffer, 256, MSG_WAITALL);
	fileName = string(buffer);
	int r = 0;
	long long bytes_recv = 0;
	while (fileName.compare("end") != 0) {
		output.open(fileName.c_str());
		//receive the content of that file over tcp
		r = recv(peer_socket_tcp, buffer + bytes_recv, 1000, MSG_WAITALL);
		while (r > 0) {
			bytes_recv += r;
			if (bytes_recv > buffer_size / 2) {
				content_buffer = (char *)realloc(content_buffer, buffer_size * 2);
				buffer_size *= 2;
			}
			r = recv(peer_socket_tcp, buffer + bytes_recv, 1000, MSG_WAITALL);
		}
		//save buffer_content into file
		string content = string(content_buffer);
		output << content;
		output.close();
		//reinitialize parameters for content receiver
		r = 0;
		bytes_recv = 0;
		memset(content_buffer, 0, buffer_size);
		//listen to the next recv file name
		memset(buffer, 0, 256);
		recv_line(peer_socket_tcp, buffer, 256, MSG_WAITALL);
		fileName = string(buffer);
	}
	return;
}

time_t filetime_to_timet(const FILETIME& ft)
{
	ULARGE_INTEGER ull;
	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;

	return ull.QuadPart / 10000000ULL - 11644473600ULL;
}