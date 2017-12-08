/*
 * gcc -lssl -lcrypto -o client client.c openssl_hostname_validation.c
 *
 * usage: ./client rhost rport
 * e.g.: ./client cic10.ie.cuhk.edu.hk 4080
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include "openssl_hostname_validation.h"

///connect to rhost:rport, and return the socket
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
    if( SSL_CTX_load_verify_locations(ctx, "ca.cert.pem", NULL) < 0)
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    printf("Loaded CA certificate: ca.cert.pem\n");
}



int main(int argc, char **argv)
{
    ///Step 1: check input and get the server hostname and port number
    if(argc != 3)
    {
        printf("usage: %s rhost rport\n", argv[0]);
        printf("example: %s cic10.ie.cuhk.edu.hk 4080\n", argv[0]);
        return -1;
    }
    char* rhost = argv[1];
    int rport = atoi(argv[2]);
    
    ///Step 2: initialize SSL
    init_openssl();
    SSL_CTX* ctx = create_context();
    configure_context(ctx);
    SSL *ssl = SSL_new(ctx);
    BIO* outbio  = BIO_new_fp(stdout, BIO_NOCLOSE);
    
    ///Step 3: connect to the server and get the socket
    int server_sock = create_socket(rhost, rport);
    
    ///Step 4: Attach the SSL session to the socket descriptor 
    SSL_set_fd(ssl, server_sock);
  
    ///Step 5: do SSL handshake to connect to the server
    if(SSL_connect(ssl) < 0)
    {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    printf("Finished SSL handshake\n");
    
    
    ///Step 6: get the remote certificate into the X509 structure
    X509* cert = SSL_get_peer_certificate(ssl);
    if (cert == NULL)
    {
        BIO_printf(outbio, "Error: Could not get a certificate from: %s:%d\n", rhost, rport);
        return -1;
    }
    else
    {
        BIO_printf(outbio, "Retrieved the server's certificate from: %s:%d\n", rhost, rport);
    }
    
    ///Step 7(optional):  display the peer certificate
    X509_NAME* certname = X509_get_subject_name(cert);
    BIO_printf(outbio, "Displaying the certificate subject data:\n");
    X509_NAME_print_ex(outbio, certname, 0, 0);
    BIO_printf(outbio, "\n");
  
    ///Step 8: verify the certificate
    if(SSL_get_verify_result(ssl) != X509_V_OK)
    {
        printf("Warning: Validation failed for certificate from: %s:%d\n", rhost, rport);
    }
    else
    {
        printf("Successfully validated the server's certificate from: %s:%d\n", rhost, rport);
    }
    
    /// Step 9: verify hostname
    
    /* Note: cic server installs OpenSSL 1.0.1e-fips, which does not support the X509_check_host() function 
     * [see https://www.openssl.org/docs/man1.0.2/crypto/X509_check_host.html]
     * To do hostname validatation, use The SSL Conservatory
     * [see https://github.com/iSECPartners/ssl-conservatory/tree/master/openssl]
     */
    
    char* hostname = "cic10.ie.cuhk.edu.hk";
    if(validate_hostname(hostname, cert) == MatchFound)
    {
        BIO_printf(outbio, "Successfully validated the server's hostname matched to: %s\n", hostname);
    }
    else
    {
        BIO_printf(outbio, "Server's hostname validation failed: %s.\n", hostname);
        return -1;
    }
     
    ///Step 10: type any data and send to the server
    printf("Type any data to send to the server...\n");
    while (1) 
	{
		char ch = getchar();
		
		if (ch == EOF)
		{
			break;
		}

		if (SSL_write(ssl, &ch, 1) < 0) 
		{
            ERR_print_errors_fp(stderr);
            exit(EXIT_FAILURE);
		}
	}
	

    ///Step 11: clean up
    close(server_sock);
    SSL_CTX_free(ctx);
    cleanup_openssl();
}