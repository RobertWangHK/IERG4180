#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <chrono>
#include <getopt.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#define SOCKET int
#define SOCKET_ERROR -1
#define SOCKADDR sockaddr
#define INVALID_SOCKET -1
#define WSAGetLastError() (errno)
#define closesocket(s) close(s)
#define Sleep(s) usleep(1000*s)
#define ioctlsocket ioctl
#define WSAEWOULDBLOCK EWOULDBLOCK
#define DWORD unsigned long

//common header files for both Windows and Linux
#include <string.h>  //for menset
#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <thread>
#include <string>
#include "util.h"
#include "Thread.h"
#include <sys/stat.h> 
#include <map>

#define DEFAULT_UPDATE_TIME 500
#define DEFAULT_HTTP_PORT_NUM "4180"
#define DEFAULT_HTTPS_PORT_NUM "4181"
#define DEFAULT_BIND_NAME INADDR_ANY
#define DEFAULT_MODE "threadpool"
#define SECOND_PER_MINUTE CLOCKS_PER_SEC
#define MAX_NUM_THREADS 20
#define DEFAULT_NUMBER_THREADS 8

using std::cout;
using std::endl;
using std::string;
using std::to_string;
std::mutex mtx_thread;//for updating original info among threads
std::mutex mtx_data;//for updating data parameters among different threads

long updateTime = DEFAULT_UPDATE_TIME;
string lHost = "INADDR_ANY";
string httpPort = DEFAULT_HTTP_PORT_NUM;
string httpsPort = DEFAULT_HTTPS_PORT_NUM;
string mode = DEFAULT_MODE;
int numThread = DEFAULT_NUMBER_THREADS;

int listen_http();
int listen_http_client();
int handle_http(SOCKET conn_socket);
int handle_http_client(SOCKET conn_socket);

void handle_client1(SOCKET conn_socket, int num);
void handle_client2(SOCKET conn_socket, int num);

struct Client_Info{
	int num_files;
	time_t lastModified[10];
	string fileName[10];
};

struct Clients{
	struct Client_Info client1;
	struct Client_Info client2;
};

struct Clients clients;
std::map<string, int> errors;

int main(int argc, char *argv[])
{

	static struct option long_options[] =
	{
		{ "stat", required_argument, 0, 1 },
		{ "lhost", required_argument, 0, 2 },
		{ "httpport", required_argument, 0, 3 },
		{ "httpsport", required_argument, 0, 4 },
		{ "server", required_argument, 0, 5 },
		{ "poolsize", required_argument, 0, 6 },
		{ 0, 0, 0, 0 }
	};
	int c;
	while ((c = getopt_long_only(argc, argv, "", long_options, 0)) != -1)
	{
		switch (c)
		{
		case 1:
			updateTime = atol(optarg);
			break;
		case 2:
			lHost = string(optarg);
			break;
		case 3:
			httpPort = string(optarg);
			break;
		case 4:
			httpsPort = string(optarg);
			break;
		case 5:
			mode = string(optarg);
			break;
		case 6:
			numThread = atoi(optarg);
			break;
		default:
			break;
		}
	}
	//start two handling threads
	printf("web browser on port 4180\n");
	printf("clients connect to 4181\n");
	std::thread http_thread(listen_http);
	std::thread http_thread_client(listen_http_client);
	http_thread.join();
	http_thread_client.join();
	return 0;	
}
int listen_http(){

    ThreadPool pool(numThread);

	sockaddr_in *TCP_Addr = new sockaddr_in;
	memset(TCP_Addr, 0, sizeof(struct sockaddr_in));
	TCP_Addr->sin_family = AF_INET;
	TCP_Addr->sin_port = htons(stoi(httpPort));
	inet_pton(AF_INET, lHost.c_str(), &(TCP_Addr->sin_addr.s_addr));

	SOCKET Http = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	bind(Http, (struct sockaddr *)TCP_Addr, sizeof(struct sockaddr_in));
	listen(Http, 5);

	while (1) {
		sockaddr_in* peer_addr = new sockaddr_in;
		memset(peer_addr, 0, sizeof(struct sockaddr_in));
		socklen_t addr_len = sizeof(struct sockaddr_in);
		SOCKET conn_socket = conn_socket = accept(Http, (struct sockaddr *)peer_addr, &addr_len);
		
		//push to pipeline
		auto result = pool.enqueue(handle_http, conn_socket);
		delete peer_addr;
	}

	delete TCP_Addr;
	TCP_Addr = 0;
	closesocket(Http);
	return 0;
}
int listen_http_client(){

    ThreadPool pool(numThread);

	sockaddr_in *TCP_Addr = new sockaddr_in;
	memset(TCP_Addr, 0, sizeof(struct sockaddr_in));
	TCP_Addr->sin_family = AF_INET;
	TCP_Addr->sin_port = htons(stoi(httpsPort));
	inet_pton(AF_INET, lHost.c_str(), &(TCP_Addr->sin_addr.s_addr));

	SOCKET Http = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	bind(Http, (struct sockaddr *)TCP_Addr, sizeof(struct sockaddr_in));
	listen(Http, 5);

	while (1) {
		sockaddr_in* peer_addr = new sockaddr_in;
		memset(peer_addr, 0, sizeof(struct sockaddr_in));
		socklen_t addr_len = sizeof(struct sockaddr_in);
		SOCKET conn_socket = conn_socket = accept(Http, (struct sockaddr *)peer_addr, &addr_len);

		//push to pipeline
		auto result = pool.enqueue(handle_http_client, conn_socket);
		delete peer_addr;
	}

	delete TCP_Addr;
	TCP_Addr = 0;
	closesocket(Http);
	return 0;
}

// this handle_http is for handling web browser request specifically.
int handle_http(SOCKET conn_socket){

	struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    int res = getpeername(conn_socket, (struct sockaddr *)&addr, &addr_size);
    char *clientip = new char[20];
    strcpy(clientip, inet_ntoa(addr.sin_addr));
    string peer_ipaddress = string(clientip);
    if (errors.find(peer_ipaddress) != errors.end() ) {
  		if(errors[peer_ipaddress] >= 2){
			sleep(300);
			errors[peer_ipaddress] = 0;
		}	
	}

	char head_buffer[1000];
	memset(head_buffer, 0, 1000);
	int num_error_login = 0;

	//retrieve the url from request
	recv_line(conn_socket, head_buffer, 1000, MSG_WAITALL);
	string header(reinterpret_cast<const char *>(head_buffer), sizeof(head_buffer) / sizeof(head_buffer[0]));
	std::size_t position = header.find("HTTP");
	string file_path = header.substr(5, position - 5);
	file_path.erase(file_path.find_last_not_of(" \n\r\t")+1);
	if (file_path.size() == 0 || file_path.compare("login.html") == 0){
		file_path = "html/login.html";
	}
	//check if login or user validation error
	if(file_path.find("login-check") != string::npos){
		unsigned position_uname = file_path.find("uname");
		unsigned position_pwd = file_path.find("pwd");

		if (!(position_uname == string::npos) && !(position_pwd == string::npos)){
			string username = file_path.substr(position_uname + 6, position_pwd - position_uname - 7);
			string password = file_path.substr(position_pwd + 4);
			if (!(username.compare("abc")==0) || !(password.compare("abc")==0)){
				file_path = "html/incorrect.html";
				struct sockaddr_in addr;
    			socklen_t addr_size = sizeof(struct sockaddr_in);
    			int res = getpeername(conn_socket, (struct sockaddr *)&addr, &addr_size);
    			char *clientip = new char[20];
    			strcpy(clientip, inet_ntoa(addr.sin_addr));
    			string peer_ipaddress = string(clientip);
    			//cout << peer_ipaddress << endl;
				if ( errors.find(peer_ipaddress) == errors.end() ) {
  					errors[peer_ipaddress] = 1;
				} else {
  					errors[peer_ipaddress] += 1;
				}
				if(errors[peer_ipaddress] >= 2){
					cout << "twice" << endl;
					file_path = "html/block.html";
					//sleep(300);
				}

			}
			else{ // success login -> home page
				file_path = "html/home.html";
			}
		}
	}

	//perform submit command to synchronize
	if(file_path.find("submit") != string::npos){
		unsigned  position_folder1 = file_path.find("Folder1");
		unsigned  position_folder2 = file_path.find("Folder2");
		// get two folder name from url request
		if (!(position_folder1 == string::npos) && !(position_folder2 == string::npos)){
		 	string name_folder1 = file_path.substr(position_folder1 + 8, position_folder2 - position_folder1 - 9);
			string name_folder2 = file_path.substr(position_folder2 + 8);
		}
		file_path = "html/hoem.html";
	}

	//if ask for client.html
	if(file_path.find("client1") != string::npos){
		file_path = "html/client1.html";
	}
	if(file_path.find("client2") != string::npos){
		file_path = "html/client2.html";
	}

	//below portion is for both retrieving html and files specifically. 
	//1. how to replace the file link in home.html
	//2. how to synchronize the two clients
	string file_header;
	string file_string;

	std::ifstream infile;
	infile.open(file_path.c_str());
	if (!infile.fail()){
		infile.open(file_path.c_str());
		file_header = "HTTP/1.0 200 OK\r\nContent-type:text/html;charset=utf8\r\n\r\n";
		std::stringstream buffer;
   		buffer << infile.rdbuf();
   		file_string = buffer.str();
   		file_string = file_header.append(file_string);
	}
	else{
		file_path = "html/404error.html";
		infile.open(file_path.c_str());
		file_header = "HTTP/1.0 200 OK\r\nContent-type:text/html;charset=utf8\r\n\r\n";
		std::stringstream buffer;
   		buffer << infile.rdbuf();
   		file_string = buffer.str();
   		file_string = file_header.append(file_string);
	}
	//replace the clients file list segment with stored information, need also attach the download link (optional)
	if (file_path.find("client1") != string::npos){
		int num = clients.client1.num_files;
		int i =0;
		string client1_html = "";
		for (;i<num; i++){
			string temp_file_name = clients.client1.fileName[i];
			client1_html += "<LI>  <A HREF=\"client1\\" +  temp_file_name + "\">temp_file_name</A>";
		}
		file_string.replace(file_string.find("@"), 1, client1_html);
	}

	if (file_path.find("client2") != string::npos){
		int num = clients.client2.num_files;
		int i =0;
		string client2_html = "";
		for (;i<num; i++){
			string temp_file_name = clients.client2.fileName[i];
			client2_html += "<LI>  <A HREF=\"client2\\" +  temp_file_name + "\">temp_file_name</A>";
		}
		file_string.replace(file_string.find("@"), 1, client2_html);
	}

   	char * content_buffer = new char[file_string.size() + 1];
	std::copy(file_string.begin(), file_string.end(), content_buffer);
	content_buffer[file_string.size()] = '\0';

	send_full(conn_socket, content_buffer, file_string.size() + 1,  0);

	close(conn_socket);
	return 0;
}

// this handle_http_client is for receiving data information from the client
int handle_http_client(SOCKET conn_socket){

	//for loop to receive and create folder hirarcy information accordingly, file name, last modified date
	char buffer[256];
	string folderName;
	string fileName;
	time_t lastModified;
	int i=0;
	string client_identity;
	//receive client folder name and create accordingly
	memset(buffer, 0, 256);
	recv_line(conn_socket, buffer, 256, MSG_WAITALL);
	folderName = string(buffer);
	mkdir(folderName.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if(folderName.compare("client1")==0){//it is from client1
		client_identity = "client1";
	}
	else if(folderName.compare("client2")==0){//it is from client1
		client_identity = "client2";
	}
	//receive first file name
	memset(buffer, 0, 256);
	recv_line(conn_socket, buffer, 256, MSG_WAITALL);
	fileName = string(buffer);
	while(fileName.compare("end")!=0){
		//recv last modified data
		memset(buffer, 0, 256);
		recv_line(conn_socket, buffer, 256, MSG_WAITALL);
		memcpy(&lastModified, buffer, sizeof(time_t));

		//save the file name and last modified information
		if(folderName.compare("client1")==0){//it is from client1
			clients.client1.fileName[i] = fileName;
			clients.client1.lastModified[i] = lastModified;
		}
		else if(folderName.compare("client2")==0){//it is from client1
			clients.client2.fileName[i] = fileName;
			clients.client2.lastModified[i] = lastModified;
		}
		//receive the next one
		memset(buffer, 0, 256);
		recv_line(conn_socket, buffer, 256, MSG_WAITALL);
		fileName = string(buffer);
		i++;
	}

	//for loop to receive files from client and save files accordingly.
	if (client_identity.compare("client1") == 0){
		handle_client1(conn_socket, i);
		clients.client1.num_files = i;
	}
	else if (client_identity.compare("client2") == 0){
		handle_client2(conn_socket, i);
		clients.client2.num_files = i;
	}

	close(conn_socket);
	return 0;

}

//for asking files from clients
void handle_client1(SOCKET peer_socket_tcp, int num){
	char buffer[256];
	memset(buffer, 0, 256);
	char * content_buffer = (char *)malloc(sizeof(char) * 1000);
	long int buffer_size = 1000;//for content_buffer length specifically
	std::ofstream output;
	snprintf(buffer, 256, "end", 0);
	send_full(peer_socket_tcp, buffer, 256, 0);

	while(num>=0){
		string file_path = clients.client1.fileName[num];
		memset(buffer, 0, 256);
		memcpy(buffer, file_path.c_str(), sizeof(file_path));
		send_full(peer_socket_tcp, buffer, 256, 0);

		//for recieving contents of the required file
		int r = 0;
		long long bytes_recv = 0;
		output.open(file_path.c_str());
		r = recv(peer_socket_tcp, buffer + bytes_recv, 1000, MSG_WAITALL);
		while (r > 0) {
			bytes_recv += r;
			if (bytes_recv > buffer_size / 2) {
				content_buffer = (char *)realloc(content_buffer, buffer_size * 2);
				buffer_size *= 2;
			}
			r = recv(peer_socket_tcp, buffer + bytes_recv, 1000, MSG_WAITALL);
		}
		string content = string(content_buffer);
		output << content;
		output.close();
		num--;
	}
}
void handle_client2(SOCKET peer_socket_tcp, int num){
	char buffer[256];
	memset(buffer, 0, 256);
	char * content_buffer = (char *)malloc(sizeof(char) * 1000);
	long int buffer_size = 1000;//for content_buffer length specifically
	std::ofstream output;
	snprintf(buffer, 256, "end", 0);
	send_full(peer_socket_tcp, buffer, 256, 0);

	while(num>=0){
		string file_path = clients.client2.fileName[num];
		memset(buffer, 0, 256);
		memcpy(buffer, file_path.c_str(), sizeof(file_path));
		send_full(peer_socket_tcp, buffer, 256, 0);

		//for recieving contents of the required file
		int r = 0;
		long long bytes_recv = 0;
		output.open(file_path.c_str());
		r = recv(peer_socket_tcp, buffer + bytes_recv, 1000, MSG_WAITALL);
		while (r > 0) {
			bytes_recv += r;
			if (bytes_recv > buffer_size / 2) {
				content_buffer = (char *)realloc(content_buffer, buffer_size * 2);
				buffer_size *= 2;
			}
			r = recv(peer_socket_tcp, buffer + bytes_recv, 1000, MSG_WAITALL);
		}
		string content = string(content_buffer);
		output << content;
		output.close();
		num--;
	}
}
