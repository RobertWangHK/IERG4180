/*
 * gcc -lssl -lcrypto -o server server.c
 *
 * usage: ./server lport
 * e.g.: ./server 4080
 * 
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

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

int main(int argc, char **argv)
{
    ///Step 1: check input and get the input port number
    if(argc != 2)
    {
        printf("usage: %s lport\n", argv[0]);
        printf("example: %s 4080\n", argv[0]);
        return -1;
    }
    int port = atoi(argv[1]);
    
    ///Step 2: initialize SSL
    init_openssl();
    SSL_CTX *ctx = create_context();
    configure_context(ctx);
    SSL* ssl = SSL_new(ctx);
    
    ///Step 3: open the port to listen the client and prepare the socket
    int listen_sock = create_socket(port);
    printf("Opened the port %d, and begin to listen...\n", port);
    
    ///Step 4: accept a TCP connection
    struct sockaddr_in addr;
    int len = sizeof(addr);
    int client_sock = accept(listen_sock, (struct sockaddr*)&addr, &len);
    if (client_sock < 0) 
    {
        perror("Unable to accept");
        return -1;
    }
    printf("Accepted a new client\n");
    
    ///Step 5: assign the socket to the SSL
    SSL_set_fd(ssl, client_sock);
    
    ///Step 6: do SSL handshake
    if (SSL_accept(ssl) <= 0) 
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    printf("Finished SSL handshake\n");
    
        
    ///Step 7: receive data from the client and display to stdout
    printf("Begin to receive data from the client...\n");
	while (1) 
	{
		char buf[256]; 
		int len = 256;
		int ret = SSL_read(ssl, buf, len-1);
		if (ret <= 0) 
		{
            ERR_print_errors_fp(stderr);
            exit(EXIT_FAILURE);
		}

        int i = 0;
		for (; i < ret; i++)
		{
			putchar(buf[i]);
		}
	}    
      
    ///Step 8: clean up    
    close(client_sock);  
    SSL_free(ssl);
    close(listen_sock);
    SSL_CTX_free(ctx);
    cleanup_openssl();
}