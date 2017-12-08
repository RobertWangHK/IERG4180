#ifndef UTIL_H
#define UTIL_H
#endif

#ifdef _WIN32

///get concise header files in windows.h
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS


#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#else // assumes Linux

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#define SOCKET int
#define SOCKET_ERROR -1
#define SOCKADDR sockaddr
#define INVALID_SOCKET -1
#define WSAGetLastError() (errno)
#define closesocket(s) close(s)
#define Sleep(s) usleep(1000*s)
#define ioctlsocket ioctl
#define WSAEWOULDBLOCK EWOULDBLOCK

#endif

#include <iostream>

///fully receive a packet of length size
int recv_full(SOCKET conn_socket, char* buf, int size, int flags);

///fully send a packet of length size
int send_full(SOCKET conn_socket, const char* buf, int size, int flags);

///recv a line to the buf,  upper bounded by max_size bytes
int recv_line(SOCKET conn_socket, char* buf, int max_size, int flags);

#pragma once
