#include "util.h"
#include <iostream>

using std::cout;
using std::endl;

int recv_full(SOCKET conn_socket, char* buf, int size, int flags)
{
	int bytes_recv = 0;
	int r = 0;
	while (bytes_recv < size)
	{
		r = recv(conn_socket, buf + bytes_recv, size - bytes_recv, flags);
		if (r > 0)
		{
			bytes_recv += r;
		}
		else
		{
			if (r == SOCKET_ERROR)
			{
				cout << "recv function failed with error code: " << WSAGetLastError() << endl;
				return -1;
			}
			else
			{
				break;
			}
		}
	}

	return 0;
}

int send_full(SOCKET conn_socket, const char* buf, int size, int flags)
{
	int bytes_sent = 0;
	int r = 0;
	while (bytes_sent < size)
	{
		r = send(conn_socket, buf + bytes_sent, size - bytes_sent, flags);

		if (r > 0)
		{
			bytes_sent += r;
		}
		else
		{
			if (r == SOCKET_ERROR)
			{
				cout << "send function failed with error code: " << WSAGetLastError() << endl;
				return -1;
			}
			break;
		}
	}

	return 0;
}

int recv_line(SOCKET conn_socket, char* buf, int max_size, int flags)
{
	int i = 0;

	while (i < max_size)
	{
		int r = recv(conn_socket, buf + i, 1, flags);

		if (r == 1)
		{
			if ( i > 3 && buf[i] == '\n' && buf[i-1] == '\r' && buf[i-2] == '\n' && buf[i-3] == '\r')
			{
				break;
			}
		}
		else if (r == SOCKET_ERROR)
		{
			cout << "recv function failed with error code: " << WSAGetLastError() << endl;
			return -1;
		}
		else
		{
			return i;
		}

		i++;
	}

	return i + 1;
}
