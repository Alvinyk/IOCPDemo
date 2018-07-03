#pragma once
#include <winsock2.h>

#define BUFFER_SIZE 1024
typedef struct _PER_HANDEL_DATA
{
	SOCKET s;
	sockaddr_in addr;
}PER_HANDLE_DATA,*PPER_HANDLE_DATA;

typedef enum _IO_OPERATION_TYPE
{
	OP_READ = 1,
	OP_WRITE = 2,
	OP_ACCEPT = 3,
}En_IO_OperationType;

typedef struct _PER_IO_DATA
{
	OVERLAPPED ol;
	char buf[BUFFER_SIZE];
	En_IO_OperationType type;
}PER_IO_DATA,*PPER_IO_DATA;
