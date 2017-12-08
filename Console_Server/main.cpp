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

void display();
void http_https_initiate();
void http_increase();
void http_decrease();
void https_increase();
void https_decrease();

int listen_http();
int listen_https();
int handle_http(SOCKET conn_socket);
int handle_https(SOCKET conn_socket, SSL* ssl);

int create_socket(int port);
void init_openssl();
void cleanup_openssl();
SSL_CTX* create_context();
void configure_context(SSL_CTX *ctx);

struct thread_data {
	int http; //number of tcp client
	int https; //number of udp client
};

struct thread_data context;

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

	http_https_initiate();

	//start the display thread
	if (updateTime != 0){
		std::thread display_thread(display);
		display_thread.detach();
	}

	//start two handling threads
	std::thread http_thread(listen_http);
	std::thread https_thread(listen_https);
	http_thread.join();
	https_thread.join();
	return 0;	
}

int listen_http(){
	//char * buffer_init = (char *)calloc(sizeof(char), 1000);
	
	//create the http socket pool
    ThreadPool pool(numThread);

	sockaddr_in *TCP_Addr = new sockaddr_in;
	memset(TCP_Addr, 0, sizeof(struct sockaddr_in));
	TCP_Addr->sin_family = AF_INET;
	TCP_Addr->sin_port = htons(stoi(httpPort));
	inet_pton(AF_INET, lHost.c_str(), &(TCP_Addr->sin_addr.s_addr));

	SOCKET Http = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	bind(Http, (struct sockaddr *)TCP_Addr, sizeof(struct sockaddr_in));
	listen(Http, 5);

	if (mode.compare("thread") == 0){
		while (1) {
			sockaddr_in* peer_addr = new sockaddr_in;
			memset(peer_addr, 0, sizeof(struct sockaddr_in));
			socklen_t addr_len = sizeof(struct sockaddr_in);
			SOCKET conn_socket = conn_socket = accept(Http, (struct sockaddr *)peer_addr, &addr_len);

			std::thread thread_new_client(handle_http, conn_socket);
			thread_new_client.detach();
			delete peer_addr;
		}
	}
	else{ //threadpool mode http
		while (1) {
			sockaddr_in* peer_addr = new sockaddr_in;
			memset(peer_addr, 0, sizeof(struct sockaddr_in));
			socklen_t addr_len = sizeof(struct sockaddr_in);
			SOCKET conn_socket = conn_socket = accept(Http, (struct sockaddr *)peer_addr, &addr_len);

			//push to pipeline
			auto result = pool.enqueue(handle_http, conn_socket);
			delete peer_addr;
		}

	}
	delete TCP_Addr;
	TCP_Addr = 0;
	closesocket(Http);
	return 0;
}

int listen_https(){
	//char * buffer_init = (char *)calloc(sizeof(char), 1000);
	//bind https socket

	init_openssl();
    SSL_CTX *ctx = create_context();
    configure_context(ctx);
    SSL* ssl = SSL_new(ctx);

	ThreadPool pool(numThread);
	int Https = create_socket(stoi(httpsPort));
	printf("Opened the port %s, and begin to listen...\n", httpsPort.c_str());

	if (mode.compare("thread") == 0){

		while (1) {
			sockaddr_in* peer_addr = new sockaddr_in;
			memset(peer_addr, 0, sizeof(struct sockaddr_in));
			socklen_t addr_len = sizeof(struct sockaddr_in);
			SOCKET conn_socket = conn_socket = accept(Https, (struct sockaddr *)peer_addr, &addr_len);

			SSL_set_fd(ssl, conn_socket);
			if (SSL_accept(ssl) <= 0) 
    		{
        		ERR_print_errors_fp(stderr);
        		exit(EXIT_FAILURE);
    		}
    		printf("Finished SSL handshake\n");

			std::thread thread_new_client(handle_https, conn_socket, ssl);
			thread_new_client.detach();
			delete peer_addr;
		}

	}
	else{ //threadpool mode https
		while (1) {
			sockaddr_in* peer_addr = new sockaddr_in;
			memset(peer_addr, 0, sizeof(struct sockaddr_in));
			socklen_t addr_len = sizeof(struct sockaddr_in);
			SOCKET conn_socket = conn_socket = accept(Https, (struct sockaddr *)peer_addr, &addr_len);

			SSL_set_fd(ssl, conn_socket);
			if (SSL_accept(ssl) <= 0) 
    		{
        		ERR_print_errors_fp(stderr);
        		exit(EXIT_FAILURE);
    		}
    		printf("Finished SSL handshake\n");

			//push to pipeline
			auto result = pool.enqueue(handle_https, conn_socket, ssl);
			delete peer_addr;
		}

	}

	closesocket(Https);
	return 0;
}

int handle_http(SOCKET conn_socket){
	
	http_increase();

	char head_buffer[1000];
	memset(head_buffer, 0, 1000);

	//string file_path = "index.html";

	recv_line(conn_socket, head_buffer, 1000, MSG_WAITALL);
	string header(reinterpret_cast<const char *>(head_buffer), sizeof(head_buffer) / sizeof(head_buffer[0]));
	std::size_t position = header.find("HTTP");
	string file_path = header.substr(5, position - 5);
	file_path.erase(file_path.find_last_not_of(" \n\r\t")+1);

	if (file_path.size() == 0){
		file_path = "index.html";
	}

	//cout << "path: " << file_path << endl;

	string file_header;
	string file_string;

	std::ifstream infile;
	infile.open(file_path.c_str());
	if (!infile.fail()){
		infile.open(file_path.c_str());
		file_header = "Status: 200 OK\r\n\r\n";
		std::stringstream buffer;
   		buffer << infile.rdbuf();
   		file_string = buffer.str();
   		file_string = file_header.append(file_string);
	}
	else{
		file_header = "Status: 404 Not Found\r\n\r\n";
		file_string = file_header;
	}

   	char * content_buffer = new char[file_string.size() + 1];
	std::copy(file_string.begin(), file_string.end(), content_buffer);
	content_buffer[file_string.size()] = '\0';

	//cout << "content buffer: " << content_buffer << endl;

	send_full(conn_socket, content_buffer, file_string.size() + 1,  0);

	http_decrease();
	close(conn_socket);
	return 0;

}
int handle_https(SOCKET conn_socket, SSL* ssl){
	https_increase();
	//handle here
	char head_buffer[1000];
	memset(head_buffer, 0, 1000);

	int ret = SSL_read(ssl, head_buffer, 1000);
	if (ret <= 0) 
	{
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
	}

	string header(reinterpret_cast<const char *>(head_buffer), sizeof(head_buffer) / sizeof(head_buffer[0]));
	std::size_t position = header.find("HTTP");
	string file_path = header.substr(5, position - 5);
	file_path.erase(file_path.find_last_not_of(" \n\r\t")+1);

	if (file_path.size() == 0){
		file_path = "index.html";
	}

	//cout << "path: " << file_path << endl;

	string file_header;
	string file_string;

	std::ifstream infile;
	infile.open(file_path.c_str());
	if (!infile.fail()){
		infile.open(file_path.c_str());
		file_header = "Status: 200 OK\r\n\r\n";
		std::stringstream buffer;
   		buffer << infile.rdbuf();
   		file_string = buffer.str();
   		file_string = file_header.append(file_string);
	}
	else{
		file_header = "Status: 404 Not Found\r\n\r\n";
		file_string = file_header;
	}

   	char * content_buffer = new char[file_string.size() + 1];
	std::copy(file_string.begin(), file_string.end(), content_buffer);
	content_buffer[file_string.size()] = '\0';

	//cout << "content buffer: " << content_buffer << endl;

	if (SSL_write(ssl, content_buffer, file_string.size() + 1) < 0) 
	{
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
	}

	https_decrease();
	close(conn_socket);
}

void display() {
	Sleep(5*1000);
	setbuf(stdout, NULL);
	std::chrono::time_point<std::chrono::system_clock> start, end;
    start = std::chrono::system_clock::now();
	int http = 0;
	int https = 0;
	while (1) {	
		end = std::chrono::system_clock::now();
		//cout << "time" << end - start << endl;
		int elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(end-start).count() + 5;
		int elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
		//cout << elapsed_milliseconds;
		//cout << elapsed_seconds;
		mtx_thread.lock();
		http = context.http;
		https = context.https;
		mtx_thread.unlock();
		printf("\rElapsed [%ds] HTTP Clients [%d] HTTPS Clients [%d]", elapsed_seconds, http, https);
		Sleep(updateTime);
	}
	return;
}

void http_https_initiate(){
	mtx_thread.lock();
	context.http = 0;
	context.https = 0;
	mtx_thread.unlock();
}
void http_increase(){
	mtx_thread.lock();
	context.http += 1;
	mtx_thread.unlock();
}
void http_decrease(){
	mtx_thread.lock();
	context.http -= 1;
	mtx_thread.unlock();
}
void https_increase(){
	mtx_thread.lock();
	context.https += 1;
	mtx_thread.unlock();
}
void https_decrease(){
	mtx_thread.lock();
	context.https -= 1;
	mtx_thread.unlock();
}

int create_socket(int port)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) 
    {
        perror("Unable to create socket");
        exit(EXIT_FAILURE);
    }

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) 
    {
        perror("Unable to bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_sock, 1) < 0) 
    {
        perror("Unable to listen");
        exit(EXIT_FAILURE);
    }

    return listen_sock;
}

void init_openssl()
{ 
    SSL_load_error_strings();	
    OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl()
{
    EVP_cleanup();
}

SSL_CTX* create_context()
{
    const SSL_METHOD* method = SSLv23_server_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) 
    {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    return ctx;
}

void configure_context(SSL_CTX *ctx)
{
    /// Set the server private key and server cert
    if (SSL_CTX_use_certificate_file(ctx, "certs/server.cert.signed.pem", SSL_FILETYPE_PEM) < 0)
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    printf("Loaded server certificate: certs/server.cert.signed.pem\n");

    if (SSL_CTX_use_PrivateKey_file(ctx, "private/server.key.pem", SSL_FILETYPE_PEM) < 0 ) 
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    printf("Loaded server private key: private/server.key.pem\n");
}