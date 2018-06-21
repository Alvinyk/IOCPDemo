#include "CIOCPServer.h"

const CInitSock CIOCPServer::sm_wsSocket;

CIOCPServer::CIOCPServer()
{
	InitList();
	InitCount();
	InitHandle();
	InitCfg();
	InitCriticalSection();
	m_Shutdown = FALSE;
	m_Started = FALSE;
}

void CIOCPServer::InitList()
{
	m_pFreeBufferList = NULL;
	m_pFreeContextList = NULL;
	m_pPendingAcceptList = NULL;
	m_pConnectionList = NULL;
}

void CIOCPServer::InitCount()
{
	m_FreeBufferCount = 0;
	m_FreeContextCount = 0;
	m_PendingAcceptCount = 0;
	m_CurrentConnection = 0;
	m_ReportCount = 0;
}

void CIOCPServer::InitCriticalSection()
{
	::InitializeCriticalSection(&m_FreeBufferListLock);
	::InitializeCriticalSection(&m_FreeContextListLock);
	::InitializeCriticalSection(&m_PendingAcceptListLock);
	::InitializeCriticalSection(&m_ConnectionListLock);
}

void CIOCPServer::InitHandle()
{
	m_hAcceptEvent = ::CreateEvent(NULL,FALSE,FALSE,NULL);
	m_hRepostEvent = ::CreateEvent(NULL,FALSE,FALSE,NULL);
	m_hListenThread = NULL;
	m_hCompletion = NULL;
	m_sListen = INVALID_SOCKET;
	m_lpfnAcceptEx = NULL;
	m_lpfnGetAcceptExSockaddrs = NULL;
}

void CIOCPServer::InitCfg()
{
	m_port = 4567;
	m_InitialAccepts = 10;
	m_InitialReads = 4;
	m_MaxAccepts = 100;
	m_MaxSends = 20;
	m_MaxConnections = 2000;
	m_FreeBufferCount = 200;
	m_FreeContextCount = 100;
}

CIOCPServer::~CIOCPServer()
{
	Shutdown();
	CloseAllHandle();
	DeleteCriticalSection();
}

void CIOCPServer::Shutdown()
{
	if(m_Started)
	{
		m_Shutdown = TRUE;
		::SetEvent(m_hAcceptEvent);
		::WaitForSingleObject(m_hListenThread,INFINITE);
		m_hListenThread = NULL;
	}
}

void CIOCPServer::CloseAllHandle()
{
	if(m_sListen != INVALID_SOCKET)
		::closesocket(m_sListen);
	if(m_hListenThread != NULL)
		::CloseHandle(m_hListenThread);

	::CloseHandle(m_hRepostEvent);
	::CloseHandle(m_hAcceptEvent);
}

void CIOCPServer::DeleteCriticalSection()
{
	::DeleteCriticalSection(&m_FreeBufferListLock);
	::DeleteCriticalSection(&m_FreeContextListLock);
	::DeleteCriticalSection(&m_PendingAcceptListLock);
	::DeleteCriticalSection(&m_ConnectionListLock);
}

CIOCPBuffer* CIOCPServer::AllocateBuffer(int len)
{
	CIOCPBuffer *pBuffer = NULL;
	if(len > BUFFER_SIZE)
		return NULL;

	::EnterCriticalSection(&m_FreeBufferListLock);
	if(m_pFreeBufferList == NULL)
	{
		pBuffer = (CIOCPBuffer *)::HeapAlloc(::GetProcessHeap()
			,HEAP_ZERO_MEMORY,sizeof(CIOCPBuffer) + BUFFER_SIZE);
	}else
	{
		pBuffer = m_pFreeBufferList;
		m_pFreeBufferList = m_pFreeBufferList->pNext;
		pBuffer->pNext = NULL;
		m_FreeBufferCount--;
	}
	::LeaveCriticalSection(&m_FreeBufferListLock);

	if(pBuffer != NULL)
	{
		pBuffer->buff = (char*)(pBuffer + 1);
		pBuffer->nlen = len;
	}

	return pBuffer;
}

void CIOCPServer::ReleaseBuffer(CIOCPBuffer *pBuffer)
{
	::EnterCriticalSection(&m_FreeBufferListLock);

	if(m_FreeBufferCount < m_MaxFreeBuffers)
	{
		memset(pBuffer,0,sizeof(CIOCPBuffer) + BUFFER_SIZE);
		pBuffer->pNext = m_pFreeBufferList;
		m_pFreeBufferList = pBuffer;
		m_FreeBufferCount++;
	}else
	{
		::HeapFree(::GetProcessHeap(),0,pBuffer);
	}
	::LeaveCriticalSection(&m_FreeBufferListLock);
}

CIOCPContext *CIOCPServer::AllocateContext(SOCKET s)
{
	CIOCPContext *pContext;

	::EnterCriticalSection(&m_FreeContextListLock);

	if(m_pFreeContextList == NULL)
	{
		pContext = (CIOCPContext *)::HeapAlloc(::GetProcessHeap()
			,HEAP_ZERO_MEMORY,sizeof(CIOCPContext));
		::InitializeCriticalSection(&pContext->lock);
	}else
	{
		pContext = m_pFreeContextList;
		m_pFreeContextList = m_pFreeContextList->pNext;
		pContext->pNext = NULL;
		m_FreeContextCount--;
	}
	::LeaveCriticalSection(&m_FreeContextListLock);

	if(pContext != NULL)
	{
		pContext->s = s;
	}
	return pContext;
}

void CIOCPServer::ReleaseContext(CIOCPContext *pContext)
{
	if(pContext->s != INVALID_SOCKET)
		::closesocket(pContext->s);

	//首先释放此套接字上的没有按顺序完成的读IO的缓冲区
	CIOCPBuffer *pBuffer;
	while(pContext->pOutOfOrderReads != NULL)
	{
		pBuffer = pContext->pOutOfOrderReads->pNext;
		ReleaseBuffer(pContext->pOutOfOrderReads);
		pContext->pOutOfOrderReads = pBuffer;
	}

	::EnterCriticalSection(&m_FreeContextListLock);
	if (m_FreeContextCount < m_MaxFreeContexts)
	{
		CRITICAL_SECTION cstmp = pContext->lock;
		memset(pContext,0,sizeof(CIOCPContext));
		pContext->lock = cstmp;
		pContext->pNext = m_pFreeContextList;
		m_pFreeContextList = pContext;
		m_FreeContextCount++;
	}else
	{
		::DeleteCriticalSection(&pContext->lock);
		::HeapFree(::GetProcessHeap(),0,pContext);
	}
	::LeaveCriticalSection(&m_FreeContextListLock);
}

void CIOCPServer::FreeBuffers()
{
	::EnterCriticalSection(&m_FreeBufferListLock);

	CIOCPBuffer *pFreeBuffer = m_pFreeBufferList;
	CIOCPBuffer *pNextBuffer;
	while(pFreeBuffer != NULL)
	{
		pNextBuffer = pFreeBuffer->pNext;
		::HeapFree(::GetProcessHeap(),0,pFreeBuffer);
		pFreeBuffer = pNextBuffer;
	}
	m_pFreeBufferList = NULL;
	m_FreeBufferCount = 0;
	::LeaveCriticalSection(&m_FreeBufferListLock);
}

void CIOCPServer::FreeContexts()
{
	::EnterCriticalSection(&m_FreeContextListLock);

	CIOCPContext *pFreeContext = m_pFreeContextList;
	CIOCPContext *pNextContext = NULL;
	while(pFreeContext != NULL)
	{
		pNextContext = pFreeContext->pNext;
		::DeleteCriticalSection(&pFreeContext->lock);
		::HeapFree(::GetProcessHeap(),0,pFreeContext);
		pFreeContext = pNextContext;
	}
	m_pFreeContextList = NULL;
	m_FreeContextCount = 0;
	::LeaveCriticalSection(&m_FreeContextListLock);
}

BOOL CIOCPServer::AddAConnection(CIOCPContext *pContext)
{
	::EnterCriticalSection(&m_ConnectionListLock);

	if(m_CurrentConnection < m_MaxConnections)
	{
		pContext->pNext = m_pConnectionList;
		m_pConnectionList = pContext;
		m_CurrentConnection++;
		::LeaveCriticalSection(&m_ConnectionListLock);
		return TRUE;
	}
	::LeaveCriticalSection(&m_ConnectionListLock);
	return FALSE;
}

void CIOCPServer::CloseAConnection(CIOCPContext *pContext)
{
	::EnterCriticalSection(&m_ConnectionListLock);
	CIOCPContext *pTest = m_pConnectionList;
	if (pTest == pContext)
	{
		m_pConnectionList = pContext->pNext;
		m_CurrentConnection--;
	}else
	{
		while(pTest != NULL && pTest->pNext != pContext)
		{
			pTest = pTest->pNext;
		}

		if (pTest != NULL)
		{
			pTest->pNext = pContext->pNext;
			m_CurrentConnection--;
		}
	}
	::LeaveCriticalSection(&m_ConnectionListLock);

	::EnterCriticalSection(&pContext->lock);
	if(pContext->s != INVALID_SOCKET)
	{
		::closesocket(pContext->s);
		pContext->s = INVALID_SOCKET;
	}
	pContext->bClosing = TRUE;
	::LeaveCriticalSection(&pContext->lock);
}

void CIOCPServer::CloseAllConnections()
{
	::EnterCriticalSection(&m_ConnectionListLock);
	CIOCPContext *pContext = m_pConnectionList;
	while(pContext != NULL)
	{
		::EnterCriticalSection(&pContext->lock);
		if(pContext->s != INVALID_SOCKET)
		{
			::closesocket(pContext->s);
			pContext->s = INVALID_SOCKET;
		}
		pContext->bClosing = TRUE;
		::LeaveCriticalSection(&pContext->lock);

		pContext = pContext->pNext;
	}

	m_pConnectionList = NULL;
	m_CurrentConnection = 0;
	::LeaveCriticalSection(&m_ConnectionListLock);
}

BOOL CIOCPServer::InsertPendingAccept(CIOCPBuffer *pBuffer)
{
	::EnterCriticalSection(&m_PendingAcceptListLock);
	if (m_pPendingAcceptList == NULL)
	{
		m_pPendingAcceptList = pBuffer;
	}else
	{
		pBuffer->pNext = m_pPendingAcceptList;
		m_pPendingAcceptList = pBuffer;
	}
	m_PendingAcceptCount++;
	::LeaveCriticalSection(&m_PendingAcceptListLock);

	return TRUE;
}

BOOL CIOCPServer::RemovePendingAccept(CIOCPBuffer *pBuffer)
{
	BOOL bResult = FALSE;

	::EnterCriticalSection(&m_PendingAcceptListLock);
	
	CIOCPBuffer *pTest = m_pPendingAcceptList;
	if(pTest == pBuffer)
	{
		m_pPendingAcceptList = pBuffer->pNext;
		bResult = TRUE;
	}else
	{
		while(pTest != NULL && pTest->pNext != pBuffer)
		{
			pTest = pTest->pNext;
		}
		if(pTest != NULL)
		{
			pTest->pNext = pBuffer->pNext;
			bResult = TRUE;
		}
	}

	if(bResult)
	{
		m_PendingAcceptCount--;
	}
	::LeaveCriticalSection(&m_PendingAcceptListLock);

	return bResult;
}

CIOCPBuffer* CIOCPServer::GetNextReadBuffer(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
	if(pBuffer != NULL)
	{
		//如果与要读的下一个序列号相等，则读这块缓冲区
		if(pBuffer->sequence == pContext->currentReadSequence)
		{
			return pBuffer;
		}

		//如果不想等，则说明没有按顺序接收数据，将这块缓冲区保存到连接的OutOfOrderReads中
		//列表中的缓冲区时按照其序列号从小到大的顺序排列的
		pBuffer->pNext = NULL;
		CIOCPBuffer* ptr = pContext->pOutOfOrderReads;
		CIOCPBuffer* pPre = NULL;
		while(ptr != NULL)
		{
			if(pBuffer->sequence < ptr->sequence)
				break;

			pPre = ptr;
			ptr = ptr->pNext;
		}

		//应该插入到表头
		if (ptr == NULL && pPre != NULL)
		{
			pBuffer->pNext = NULL;
			pPre->pNext = pBuffer;
		}else
		{
			pBuffer->pNext = pPre->pNext;
			pPre->pNext = pBuffer;
		}
	}

	//检查表头元素的序列号，如果与要读的序列号一致，就将它从表中移除，返回给用户
	CIOCPBuffer *pReadBuffer = pContext->pOutOfOrderReads;
	if(pReadBuffer!=NULL && (pReadBuffer->sequence == pContext->currentReadSequence))
	{
		pContext->pOutOfOrderReads = pReadBuffer->pNext;
		return pReadBuffer;
	}

	return NULL;
}

BOOL CIOCPServer::PostAccept(CIOCPBuffer *pBuffer)
{
	pBuffer->operation = OP_ACCETP;
	DWORD dwBytes;
	pBuffer->sClient = ::WSASocket(AF_INET,SOCK_STREAM,0,NULL,0,WSA_FLAG_OVERLAPPED);
	BOOL bAccept = m_lpfnAcceptEx(m_sListen
		,pBuffer->sClient
		,pBuffer->buff
		,pBuffer->nlen - ((sizeof(sockaddr_in) + 16) * 2)
		,sizeof(sockaddr_in) + 16
		,sizeof(sockaddr_in) + 16
		,&dwBytes
		,&pBuffer->ol);
	if(!bAccept && ::WSAGetLastError() != WSA_IO_PENDING)
	{
		return FALSE;
	}
	return TRUE;
}

BOOL CIOCPServer::PostRecv(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
{
	pBuffer->operation = OP_READ;
	::EnterCriticalSection(&pContext->lock);

	pBuffer->sequence = pContext->readSequence;
	DWORD dwBytes = 0;
	DWORD dwFlag = 0;
	WSABUF buf;
	buf.buf = pBuffer->buff;
	buf.len = pBuffer->nlen;

	if(::WSARecv(pContext->s,&buf,1,&dwBytes,&dwFlag,&pBuffer->ol,NULL) != NO_ERROR)
	{
		if(::WSAGetLastError() != WSA_IO_PENDING)
		{
			::LeaveCriticalSection(&pContext->lock);
			return FALSE;
		}
	}

	pContext->outStandingRecv++;
	pContext->readSequence++;

	::LeaveCriticalSection(&pContext->lock);
	
	return TRUE;
}

BOOL CIOCPServer::PostSend(CIOCPContext* pContext, CIOCPBuffer* pBuffer)
{
	if(pContext->outStandingSend > m_MaxSends)
		return FALSE;

	pBuffer->operation = OP_WRITE;

	DWORD dwBytes = 0;
	DWORD dwFlags = 0;
	WSABUF buf;
	buf.buf = pBuffer->buff;
	buf.len = pBuffer->nlen;
	if(::WSASend(pContext->s,&buf,1,&dwBytes,dwFlags,&pBuffer->ol,NULL) != NO_ERROR)
	{
		if(::WSAGetLastError() != WSA_IO_PENDING)
			return FALSE;
	}

	::EnterCriticalSection(&pContext->lock);
	pContext->outStandingSend++;
	::LeaveCriticalSection(&pContext->lock);

	return TRUE;
}

BOOL CIOCPServer::Start(char *pBindAddr, int port)
{
	if(m_Started)
	{
		return FALSE;
	}

	m_port = port;
	m_Shutdown = FALSE;
	m_Started = TRUE;

	m_sListen = ::WSASocket(AF_INET,SOCK_STREAM,0,NULL,0,WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN si;
	si.sin_family = AF_INET;
	si.sin_port = ::ntohs(m_port);
	si.sin_addr.S_un.S_addr = INADDR_ANY;

	if(::bind(m_sListen,(sockaddr*)&si,sizeof(si)) == SOCKET_ERROR)
	{
		m_Started = FALSE;
		return FALSE;
	}

	::listen(m_sListen,200);

	m_hCompletion = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE,0,0,0);

	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	DWORD dwBytes;
	::WSAIoctl(m_sListen,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx,
		sizeof(GuidAcceptEx),
		&m_lpfnAcceptEx,
		sizeof(m_lpfnAcceptEx),
		&dwBytes,
		NULL,
		NULL);

	GUID GuidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	::WSAIoctl(m_sListen,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidGetAcceptExSockaddrs,
		sizeof(GuidGetAcceptExSockaddrs),
		&m_lpfnGetAcceptExSockaddrs,
		sizeof(m_lpfnGetAcceptExSockaddrs),
		&dwBytes,
		NULL,
		NULL);

	::CreateIoCompletionPort((HANDLE)m_sListen,m_hCompletion,0,0);

	WSAEventSelect(m_sListen,m_hAcceptEvent,FD_ACCEPT);

	m_hListenThread = ::CreateThread(NULL,0,_ListenThreadProc,this,0,NULL);

	return TRUE;
}


DWORD WINAPI CIOCPServer::_ListenThreadProc(LPVOID lpParam)
{
	CIOCPServer *pServer = (CIOCPServer*) lpParam;

	CIOCPBuffer *pBuffer;
	for(int i = 0; i< pServer->m_InitialAccepts; i++)
	{
		pBuffer = pServer->AllocateBuffer(BUFFER_SIZE);
		if (pBuffer == NULL)
		{
			return -1;
		}

		pServer->InsertPendingAccept(pBuffer);
		pServer->PostAccept(pBuffer);
	}


	HANDLE hWaitEvents[2 + MAX_THREAD];
	int nEventCount = 0;
	int Offset2WorkerThread = 2;

	hWaitEvents[nEventCount++] = pServer->m_hAcceptEvent;
	hWaitEvents[nEventCount++] = pServer->m_hRepostEvent;

	for (int i = 0; i<MAX_THREAD; i++)
	{
		hWaitEvents[nEventCount++] = ::CreateThread(NULL,0,_WorkerThreadProc,pServer,0,NULL);
	}

	while(TRUE)
	{
		int nIndex = ::WSAWaitForMultipleEvents(nEventCount,hWaitEvents,FALSE,60*1000,FALSE);

		if(pServer->m_Shutdown || nIndex == WSA_WAIT_FAILED)
		{
			pServer->CloseAllConnections();
			::Sleep(0);
			::closesocket(pServer->m_sListen);
			pServer->m_sListen = INVALID_SOCKET;
			::Sleep(0);

			for(int i = 0; i<MAX_THREAD; i++)
			{
				::PostQueuedCompletionStatus(pServer->m_hCompletion,-1,0,NULL);
			}

			::WaitForMultipleObjects(MAX_THREAD,&hWaitEvents[Offset2WorkerThread],TRUE,5*1000);

			for (int i = 0; i<MAX_THREAD; i++)
			{
				::CloseHandle(hWaitEvents[i+Offset2WorkerThread]);
			}

			::CloseHandle(pServer->m_hCompletion);
			pServer->FreeBuffers();
			pServer->FreeContexts();
			::ExitThread(0);
		}

		if (nIndex == WSA_WAIT_TIMEOUT)
		{
			pBuffer = pServer->m_pPendingAcceptList;
			while(pBuffer != NULL)
			{
				int seconds;
				int len = sizeof(seconds);
				::getsockopt(pBuffer->sClient,SOL_SOCKET,SO_CONNECT_TIME,(char*)&seconds,&len);

				if(seconds != -1 && seconds > 2*60)
				{
					::closesocket(pBuffer->sClient);
					pBuffer->sClient = INVALID_SOCKET;
				}

				pBuffer = pBuffer->pNext;
			}
		}else
		{
			nIndex = nIndex - WAIT_OBJECT_0;
			WSANETWORKEVENTS ne;
			int limit = 0;
			if(nIndex == 0)
			{
				::WSAEnumNetworkEvents(pServer->m_sListen,hWaitEvents[nIndex],&ne);
				if(ne.lNetworkEvents & FD_ACCEPT)
				{
					limit = 50;
				}
			}else if (nIndex == 1)
			{
				limit = InterlockedExchange(&pServer->m_ReportCount,0);
			}else if (nIndex > 1)
			{
				pServer->m_Shutdown = TRUE;
				continue;
			}

			for (int i = 0; i<limit && pServer->m_PendingAcceptCount < pServer->m_MaxAccepts; i++)
			{
				pBuffer = pServer->AllocateBuffer(BUFFER_SIZE);
				if (pBuffer != NULL)
				{
					pServer->InsertPendingAccept(pBuffer);
					pServer->PostAccept(pBuffer);
				}
			}
		}
	}
	return 0;
}

DWORD WINAPI CIOCPServer::_WorkerThreadProc(LPVOID lpParam)
{
	CIOCPServer *pServer = (CIOCPServer*) lpParam;

	CIOCPBuffer *pBuffer;
	DWORD key;
	DWORD trans;
	LPOVERLAPPED pOl;

	while(TRUE)
	{
		BOOL bOk = ::GetQueuedCompletionStatus(pServer->m_hCompletion,&trans
			,(LPDWORD)&key,(LPOVERLAPPED*)&pOl,WSA_INFINITE);

		if (trans == -1)
		{
			::ExitThread(0);
		}

		pBuffer = CONTAINING_RECORD(pOl,CIOCPBuffer,ol);
		int error = NO_ERROR;
		if(bOk == FALSE)
		{
			SOCKET s;
			if(pBuffer->operation == OP_ACCETP)
			{
				s = pServer->m_sListen;
			}else
			{
				if(key == 0)
					break;
				s = ((CIOCPContext*)key)->s;
			}

			DWORD falgs = 0;
			if(::WSAGetOverlappedResult(s,&pBuffer->ol,&trans,FALSE,&falgs) == FALSE)
			{
				error = ::WSAGetLastError();
			}
		}
		pServer->HandleIO((CIOCPContext*)key,pBuffer,trans,error);
	}

	return 0;
}

void CIOCPServer::HandleIO(CIOCPContext *pContext, CIOCPBuffer *pBuffer, DWORD dwTrans, int error)
{
	if (pContext != NULL)
	{
		::EnterCriticalSection(&pContext->lock);
		if (pBuffer->operation == OP_READ)
		{
			pContext->outStandingRecv--;
		}else if(pBuffer->operation == OP_WRITE)
		{
			pContext->outStandingSend--;
		}
		::LeaveCriticalSection(&pContext->lock);

		if (pContext->bClosing)
		{
			if(pContext->outStandingRecv == 0 && pContext->outStandingSend == 0)
			{
				ReleaseContext(pContext);
			}

			ReleaseBuffer(pBuffer);
			return;
		}
	}else
	{
		RemovePendingAccept(pBuffer);
	}


	if (error != NO_ERROR)
	{
		if (pBuffer->operation != OP_ACCETP)
		{
			OnConnectionError(pContext,pBuffer,error);
			CloseAConnection(pContext);
			if (pContext->outStandingRecv == 0 && pContext->outStandingSend == 0)
			{
				ReleaseContext(pContext);
			}
		}else
		{
			if (pBuffer->sClient != INVALID_SOCKET)
			{
				::closesocket(pBuffer->sClient);
				pBuffer->sClient = INVALID_SOCKET;
			}
		}

		ReleaseBuffer(pBuffer);
		return;
	}

	if (pBuffer->operation = OP_ACCETP)
	{
		if (dwTrans == 0)
		{
			if (pBuffer->sClient != INVALID_SOCKET)
			{
				::closesocket(pBuffer->sClient);
				pBuffer->sClient = INVALID_SOCKET;
			}
		}else
		{
			CIOCPContext *pClient = AllocateContext(pBuffer->sClient);
			if (pClient != NULL)
			{
				if(AddAConnection(pClient))
				{
					int localLen,remoteLen;
					LPSOCKADDR pLocalAddr,pRemoteAddr;
					m_lpfnGetAcceptExSockaddrs(
						pBuffer->buff,
						pBuffer->nlen - ((sizeof(sockaddr_in) + 16) + 2),
						sizeof(sockaddr_in) + 16,
						sizeof(sockaddr_in) + 16,
						(SOCKADDR **)&pLocalAddr,
						&localLen,
						(SOCKADDR **)&pRemoteAddr,
						&remoteLen);

					memcpy(&pClient->addrLocal,pLocalAddr,localLen);
					memcpy(&pClient->addrRemote,pRemoteAddr,remoteLen);

					::CreateIoCompletionPort((HANDLE)pClient->s,m_hCompletion,(DWORD)pClient,0);

					pBuffer->nlen = dwTrans;
					OnConnectionEstablished(pClient,pBuffer);

					for(int i = 0; i<5; i++)
					{
						CIOCPBuffer *pBuffer = AllocateBuffer(BUFFER_SIZE);
						if (pBuffer != NULL)
						{
							if (PostRecv(pClient,pBuffer) == FALSE)
							{
								CloseAConnection(pClient);
								break;
							}
						}
					}
				}else
				{
					CloseAConnection(pClient);
					ReleaseContext(pClient);
				}
			}else
			{
				::closesocket(pBuffer->sClient);
				pBuffer->sClient = INVALID_SOCKET;
			}
		}
		ReleaseBuffer(pBuffer);

		::InterlockedIncrement(&m_ReportCount);
		::SetEvent(m_hRepostEvent);
	}else if (pBuffer->operation == OP_READ)
	{
		if (dwTrans == 0)
		{
			pBuffer->nlen = 0;
			OnConnectionClosing(pContext,pBuffer);
			CloseAConnection(pContext);
			if(pContext->outStandingRecv == 0 && pContext->outStandingSend == 0)
			{
				ReleaseContext(pContext);
			}
			ReleaseBuffer(pBuffer);
		}else
		{
			pBuffer->nlen = dwTrans;

			CIOCPBuffer *pNext = GetNextReadBuffer(pContext,pBuffer);
			while (pNext != NULL)
			{
				OnReadCompleted(pContext,pNext);
				::InterlockedIncrement((LONG*)&pContext->currentReadSequence);
				ReleaseBuffer(pNext);
				pNext = GetNextReadBuffer(pContext,NULL);
			}

			CIOCPBuffer *pNew = AllocateBuffer(BUFFER_SIZE);
			if (pBuffer == NULL || !PostRecv(pContext,pBuffer))
			{
				CloseAConnection(pContext);
			}
		}
	} 
	else if (pBuffer->operation == OP_WRITE)
	{
		if (dwTrans == 0)
		{
			pBuffer->nlen = 0;
			OnConnectionClosing(pContext,pBuffer);
			CloseAConnection(pContext);

			if(pContext->outStandingRecv == 0 && pContext->outStandingSend == 0)
			{
				ReleaseContext(pContext);
			}
			ReleaseBuffer(pBuffer);
		}else
		{
			pBuffer->nlen = dwTrans;
			OnWriteCompleted(pContext,pBuffer);
			ReleaseBuffer(pBuffer);
		}
	}
}

BOOL CIOCPServer::SendText(CIOCPContext *pContext, char *pszText, int len)
{
	CIOCPBuffer *pBuffer = AllocateBuffer(len);
	if (pBuffer != NULL)
	{
		memcpy(pBuffer->buff,pszText,len);
		return PostSend(pContext,pBuffer);
	}

	return FALSE;
}