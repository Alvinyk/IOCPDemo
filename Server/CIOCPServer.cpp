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
	if(m_Started == FALSE) return;

	m_Shutdown = TRUE;
	::SetEvent(m_hAcceptEvent);
	::WaitForSingleObject(m_hListenThread,INFINITE);
	m_hListenThread = NULL;

	m_Started = FALSE;
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
