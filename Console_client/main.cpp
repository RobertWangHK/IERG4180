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
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include "openssl_hostname_validation.h"
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
#include <mutex>
#include <thread>
#include <string>
#include "util.h"

using std::cout;
using std::endl;
using std::string;
using std::to_string;
std::mutex mtx_thread;

string fileName = "";
string hostname = ""; // for verifyhostname

//these are for extracting urls
string port = "";
string host = "";
string file_path = "";
string type = ""; //http or https 

void parse_url(string url);
int http_request();
int https_request();
void display(int elapsed_milliseconds, int header_bytes, unsigned content_bytes);

int create_socket(const char* rhost, int rport);
void init_openssl();
void cleanup_openssl();
SSL_CTX* create_context();
void configure_context(SSL_CTX *ctx);

int main(int argc, char *argv[])
{
	string url = string(argv[1]);
	cout << url << endl;
	if (argc < 2)
	{
		cout << "not enough parameters" << endl;
		return -1;
	}
	static struct option long_options[] =
	{
		{ "file", required_argument, 0, 1 },
		{ "verifyhost ", required_argument, 0, 2 },
		{ 0, 0, 0, 0 }
	};
	int c;
	while ((c = getopt_long_only(argc, argv, "", long_options, 0)) != -1)
	{
		switch (c)
		{
		case 1:
			fileName = string(optarg);
			break;
		case 2:
		{
			hostname = string(optarg);
			//string temp_str = string(optarg);
			//hostname = new char[temp_str.length() + 1];
			//strcpy(hostname, temp_str.c_str());
			break;
		}
		default:
			break;
		}
	}
	//cout << fileName << endl;
	//extract url, port and file extension from url
	parse_url(url);
	if (file_path.size() == 0){
		file_path = "/";
	}
	cout << type << endl;
	cout << file_path << endl;
	cout << port << endl;
	cout << host << endl;

	if (hostname.compare("") != 0){
		host = hostname;
	}

	//start corresponding thread to start 
	if (type.compare("http") == 0) {
		if (port.compare("") == 0){
			port = string("80");
		}
		//cout << "start";
		http_request();
	}
	else if (type.compare("https") == 0) {
		if (port.compare("") == 0){
			port = string("443");
		}
		https_request();
	}
	else{}
	return 0;
}

void parse_url(string url) {
	string http("http://");
	string https("https://");
  	if (url.compare(0, http.size(), http) == 0) {
  		type = string("http");
  		unsigned int pos = url.find_first_of("/:", http.size());
  		if (pos == string::npos) { //no port or path
      		pos = url.size();
    	}
    	//get host from url
    	host = url.substr(http.size(), pos-http.size());

    	if (pos < url.size() && url[pos] == ':') { //port provided
    		unsigned int ppos = url.find_first_of("/", pos);
    		if (ppos == string::npos) { //only port provided
    			pos = url.size();
    		}
    		port = url.substr(pos+1, ppos-pos-1);
    		if (ppos != string::npos) { //also path provided
    			unsigned int pppos = url.size();
    			file_path = url.substr(ppos, pppos-ppos);
    		}
    	}
    	else if (pos < url.size() && url[pos] == '/'){ //only path provided
    		unsigned int ppos = url.size();
    		file_path = url.substr(pos, ppos-pos);
    	}

  	}
  	else if (url.compare(0, https.size(), https) == 0) {
  		type = string("https");
  		unsigned int pos = url.find_first_of("/:", https.size());
  		if (pos == string::npos) { //no port or path
      		pos = url.size();
    	}
    	//get host from url
    	host = url.substr(https.size(), pos-https.size());

    	if (pos < url.size() && url[pos] == ':') { //port provided
    		unsigned int ppos = url.find_first_of("/", pos);
    		if (ppos == string::npos) { //only port provided
    			pos = url.size();
    		}
    		port = url.substr(pos+1, ppos-pos-1);
    		if (ppos != string::npos) { //also path provided
    			unsigned int pppos = url.size();
    			file_path = url.substr(ppos, pppos-ppos);
    		}
    	}
    	else if (pos < url.size() && url[pos] == '/'){ //only path provided
    		unsigned int ppos = url.size();
    		file_path = url.substr(pos, ppos-pos);
    	}
  	}
  	else {
    	//cout << "Not an HTTP url" << endl;
    	std:: cerr << "Not an HTTP/HTTPS url" << endl;
  	}
  }

int http_request(){

	std::chrono::time_point<std::chrono::system_clock> start, end;

	struct addrinfo aiHints;
	struct addrinfo *aiList = NULL;
	memset(&aiHints, 0, sizeof(aiHints));
	aiHints.ai_family = AF_INET;
	aiHints.ai_socktype = SOCK_STREAM;
	aiHints.ai_protocol = IPPROTO_TCP;
	if (getaddrinfo(host.c_str(), port.c_str(), &aiHints, &aiList) != 0)
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

	//construct request
	//int recv = 0;
	int header_bytes = 0;
	unsigned content_bytes = 0;

	char request[256];
	memset(request, 0, 256);
	char head_buffer[1000];
	memset(head_buffer, 0, 1000);
	char content_buffer[1000];
	memset(content_buffer, 0, 1000);

	start = std::chrono::system_clock::now();

	snprintf(request, 256, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", file_path.c_str(), host.c_str());
	send_full(peer_socket_tcp, request, 256, 0);

	//buffer to receive the data
	char * buffer = (char *) malloc(sizeof(char) * 1000);
	long int buffer_size = 1000;
	memset(buffer, 0, buffer_size);

	//check if file exsits
	bool file_specify = false;
	std::ofstream output;

	if (fileName.compare("") != 0){
		file_specify = true;
		output.open(fileName.c_str());
	}

	int r = 0;
	long long bytes_recv = 0;
	r = recv(peer_socket_tcp, buffer + bytes_recv, 1000, MSG_WAITALL);
	while(r > 0){
		bytes_recv += r;
		if (bytes_recv > buffer_size / 2){
			buffer = (char *)realloc(buffer, buffer_size*2);
			buffer_size *= 2;
		}
		r = recv(peer_socket_tcp, buffer + bytes_recv, 1000, MSG_WAITALL);
	}
	end = std::chrono::system_clock::now();
	//cout << buffer << endl;
	//now split the header and content part
	string temp_str = string(buffer);
	size_t position = temp_str.find("\r\n\r\n");
	size_t end_position = temp_str.size();
	string header = temp_str.substr(0, position);
	string content = temp_str.substr(position + 4, end_position - position -3);
	//cout << content << endl;

	header_bytes = header.size();
	content_bytes = content.size();

	//cout << "header_bytes: " << header_bytes << endl;
	//cout << "content_bytes: " << content_bytes << endl;

	//header.erase(header.size() - 4);
	cout << header << endl;

	//check if asked file exist
	if (file_specify){
		output << content;
		output.close();
	}
	else {
		cout << content << endl;
	}

	int elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(end-start).count();
	int elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
	display(elapsed_milliseconds, header_bytes, content_bytes);
	close(peer_socket_tcp);
	return 0;
}

int https_request(){
	//cout << "start" << endl;
	std::chrono::time_point<std::chrono::system_clock> start, end;

	init_openssl();
    SSL_CTX* ctx = create_context();
    configure_context(ctx);
    SSL *ssl = SSL_new(ctx);
    BIO* outbio  = BIO_new_fp(stdout, BIO_NOCLOSE);

    int server_sock = create_socket(host.c_str(), stoi(port));
    SSL_set_fd(ssl, server_sock);

    if(SSL_connect(ssl) < 0)
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    printf("Finished SSL handshake\n");

    X509* cert = SSL_get_peer_certificate(ssl);
    if (cert == NULL)
    {
    	const char* temp_host = host.c_str();
    	int temp_port = stoi(port);
        BIO_printf(outbio, "Error: Could not get a certificate from: %s:%d\n", temp_host, temp_port);
        return -1;
    }
    else
    {
    	const char* temp_host = host.c_str();
    	int temp_port = stoi(port);
        BIO_printf(outbio, "Retrieved the server's certificate from: %s:%d\n", temp_host, temp_port);
    }

    X509_NAME* certname = X509_get_subject_name(cert);
    BIO_printf(outbio, "Displaying the certificate subject data:\n");
    X509_NAME_print_ex(outbio, certname, 0, 0);
    BIO_printf(outbio, "\n");

     if(SSL_get_verify_result(ssl) != X509_V_OK)
    {
    	const char* temp_host = host.c_str();
    	int temp_port = stoi(port);
        printf("Warning: Validation failed for certificate from: %s:%d\n", temp_host, temp_port);
    }
    else
    {
    	const char* temp_host = host.c_str();
    	int temp_port = stoi(port);
        printf("Successfully validated the server's certificate from: %s:%d\n", temp_host, temp_port);
    }

    const char* temp_host = host.c_str();
    int temp_port = stoi(port);

    //string temp_str_1 = string("cic10.ie.cuhk.edu.hk");
	//hostname = new char[temp_str_1.length() + 1];
	//strcpy(hostname, temp_str_1.c_str());

    if(validate_hostname(temp_host, cert) == MatchFound)
    {
        BIO_printf(outbio, "Successfully validated the server's hostname matched to: %s\n", temp_host);
    }
    else
    {
        BIO_printf(outbio, "Server's hostname validation failed: %s.\n", temp_host);
        //return -1;
    }

    int header_bytes = 0;
	unsigned content_bytes = 0;

	char request[256];
	memset(request, 0, 256);
	char head_buffer[1000];
	memset(head_buffer, 0, 1000);
	char content_buffer[1000];
	memset(content_buffer, 0, 1000);

	start = std::chrono::system_clock::now();

	snprintf(request, 256, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", file_path.c_str(), host.c_str());
	//send_full(peer_socket_tcp, request, 256, 0);
	SSL_write(ssl, request, 256);

	//buffer to receive the data
	char * buffer = (char *) malloc(sizeof(char) * 1000);
	long int buffer_size = 1000;
	memset(buffer, 0, buffer_size);

	//check if file exsits
	bool file_specify = false;
	std::ofstream output;

	if (fileName.compare("") != 0){
		file_specify = true;
		output.open(fileName.c_str());
	}

	int r = 0;
	long long bytes_recv = 0;
	//r = recv(peer_socket_tcp, buffer + bytes_recv, 1000, MSG_WAITALL);
	r = SSL_read(ssl, buffer + bytes_recv, 1000);
	while(r > 0){
		bytes_recv += r;
		if (bytes_recv > buffer_size / 2){
			buffer = (char *)realloc(buffer, buffer_size*2);
			buffer_size *= 2;
		}
		//r = recv(peer_socket_tcp, buffer + bytes_recv, 1000, MSG_WAITALL);
		r = SSL_read(ssl, buffer + bytes_recv, 1000);
	}

	end = std::chrono::system_clock::now();
	//cout << buffer << endl;
	//now split the header and content part
	string temp_str = string(buffer);
	size_t position = temp_str.find("\r\n\r\n");
	size_t end_position = temp_str.size();
	string header = temp_str.substr(0, position);
	string content = temp_str.substr(position + 4, end_position - position -3);
	//cout << content << endl;

	header_bytes = header.size();
	content_bytes = content.size();

	//cout << "header_bytes: " << header_bytes << endl;
	//cout << "content_bytes: " << content_bytes << endl;

	//header.erase(header.size() - 4);
	cout << header << endl;

	//check if asked file exist
	if (file_specify){
		output << content;
		output.close();
	}
	else {
		cout << content << endl;
	}

	int elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(end-start).count();
	int elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
	display(elapsed_milliseconds, header_bytes, content_bytes);
	close(server_sock);
	return 0;
}

void display(int elapsed_milliseconds, int header_bytes, unsigned content_bytes){
	//cout << elapsed_milliseconds << endl;
	if (elapsed_milliseconds == 0){
		cout << "It takes less than 1 millisecond to transmit this file, so I have infinite transmission speed" << endl;
	}
	setbuf(stdout, NULL);
	unsigned total_bytes = header_bytes + content_bytes;
	long double total_throughput = (long double) total_bytes *1000 / elapsed_milliseconds;
	long double content_throughput = (long double)content_bytes *1000 / elapsed_milliseconds;
	printf("\rElapsed [%dms] Total [%uB, %LfBps] File [%u, %LfBps]", elapsed_milliseconds, total_bytes, total_throughput, content_bytes, content_throughput);
}

int create_socket(const char* rhost, int rport)
{
    /// Step 1: Address resolution
	struct addrinfo aiHints;
	struct addrinfo *aiList = NULL;
	
	memset(&aiHints, 0, sizeof(aiHints));
	aiHints.ai_family = AF_INET;
	aiHints.ai_socktype = SOCK_STREAM;
	aiHints.ai_protocol = IPPROTO_TCP;
	
    char rport_str[16];
    sprintf(rport_str, "%d", rport);
	if (getaddrinfo(rhost, rport_str, &aiHints, &aiList) != 0)
	{
		perror("getaddrinfo() failed");
		return -1;
	}


	/// Step 2: Create the socket and connect to the peer
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if ( connect(fd, aiList->ai_addr, sizeof(struct sockaddr)) == -1) 
	{
		perror("connect() failed");
		return -1;
	}

	printf("Connected to the server: %s: %d\n", rhost, rport);
    return fd;
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
    const SSL_METHOD* method = SSLv23_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) 
    {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    return ctx;
}

void configure_context(SSL_CTX* ctx)
{
    ///load the CA certificate 
    if( SSL_CTX_load_verify_locations(ctx, "ca-bundle.trust.crt", NULL) < 0)
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    printf("Loaded CA certificate: ca.cert.pem\n");

}