#include "LogUtil.h"
#include "initsock2.h"
#include "IoTypeDef.h"
#include "CIOCPServer.h"

CInitSock theSock;

bool bClosed( DWORD dwTrans, PPER_IO_DATA pPerIO );

DWORD WINAPI ServerThread(LPVOID lpParam)
{
	HANDLE hCompletion = (HANDLE)lpParam;
	DWORD dwTrans;
	PPER_HANDLE_DATA pPerHandle;
	PPER_IO_DATA pPerIO;

	while(TRUE)
	{
		BOOL bOk = ::GetQueuedCompletionStatus(hCompletion,&dwTrans
			,(LPDWORD)&pPerHandle,(LPOVERLAPPED*)&pPerIO,WSA_INFINITE);

		if(!bOk || bClosed(dwTrans, pPerIO))
		{
			::closesocket(pPerHandle->s);
			::GlobalFree(pPerHandle);
			::GlobalFree(pPerIO);
			continue;
		}

		switch(pPerIO->type)
		{
		case OP_READ:
			{
				in_addr addr = pPerHandle->addr.sin_addr;
				W2L(LV_INFO,"Recv Remote IP Address : %s length:%d  data:%s\n"
					,::inet_ntoa(addr),dwTrans,pPerIO->buf);

				WSABUF buf;
				buf.buf = pPerIO->buf;
				buf.len = BUFFER_SIZE;
				pPerIO->type = OP_READ;
				DWORD flag = 0;
				::WSARecv(pPerHandle->s,&buf,1,&dwTrans,&flag,&pPerIO->ol,NULL);
			}
			break;
		case OP_WRITE:
			break;
		case OP_ACCEPT:
			break;
		}
	}
}


void main()
{
	int nPort = 45678;
	HANDLE hCompletion = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE,0,0,0);
	::CreateThread(NULL,0,ServerThread,(LPVOID)hCompletion,0,0);
	SOCKET sListen = ::socket(AF_INET,SOCK_STREAM,0);
	SOCKADDR_IN si;
	si.sin_family = AF_INET;
	si.sin_port = ::ntohs(nPort);
	si.sin_addr.S_un.S_addr = INADDR_ANY;
	::bind(sListen,(sockaddr*)&si,sizeof(si));
	::listen(sListen,5);

	while(TRUE)
	{
		SOCKADDR_IN saRemote;
		int nRemoteLen = sizeof(saRemote);
		SOCKET sNew = ::accept(sListen,(sockaddr*)&saRemote,&nRemoteLen);
		PPER_HANDLE_DATA pPerHandle = (PPER_HANDLE_DATA)::GlobalAlloc(GPTR,sizeof(PER_HANDLE_DATA));
		pPerHandle->s = sNew;
		memcpy(&pPerHandle->addr,&saRemote,nRemoteLen);
		::CreateIoCompletionPort((HANDLE)pPerHandle->s,hCompletion,(DWORD)pPerHandle,0);
		PPER_IO_DATA pPerIO = (PPER_IO_DATA)::GlobalAlloc(GPTR,sizeof(PER_IO_DATA));
		pPerIO->type = OP_READ;
		WSABUF buf;
		buf.buf = pPerIO->buf;
		buf.len = BUFFER_SIZE;
		DWORD dwRecv;
		DWORD flag = 0;
		::WSARecv(pPerHandle->s,&buf,1,&dwRecv,&flag,&pPerIO->ol,NULL);
	}
}

bool bClosed( DWORD dwTrans, PPER_IO_DATA pPerIO )
{
	return dwTrans == 0 && (pPerIO->type == OP_READ || pPerIO->type == OP_WRITE);
}
