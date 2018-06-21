#pragma once
#include "../Common/initsock2.h"
#include <Mswsock.h>

#define BUFFER_SIZE  1024*4 //IO请求缓冲区
#define MAX_THREAD	 2		//IO服务线程数量

typedef enum EnOperation
{
	OP_ACCETP = 1,
	OP_WRITE  = 2,
	OP_READ   = 3,
}EN_OPERATION;

typedef struct CIOCPBuffer
{
	WSAOVERLAPPED ol;
	SOCKET sClient;
	char* buff;
	int  nlen;
	ULONG sequence;
	int operation;
	CIOCPBuffer *pNext;
}CIOCPBuffer;


typedef struct CIOCPContext
{
	SOCKET s;
	SOCKADDR_IN addrLocal;
	SOCKADDR_IN addrRemote;
	
	BOOL bClosing;

	int outStandingRecv;
	int outStandingSend;

	ULONG readSequence;
	ULONG currentReadSequence;
	CIOCPBuffer *pOutOfOrderReads;

	CRITICAL_SECTION lock;
	CIOCPContext *pNext;
}CIOCPContext;

class CIOCPServer
{
public:
	CIOCPServer();

	~CIOCPServer();

	void DeleteCriticalSection();

	void CloseAllHandle();

	//开始服务
	BOOL Start(char *pBindAddr, int port);
	
	//停止服务
	void Shutdown();

	//关闭一个连接和关闭所有连接
	void CloseAConnection(CIOCPContext *pContext);
	void CloseAllConnections();

	//获取当前的连接数量
	ULONG GetCurrentConnection();

	//向指定客户发送文本
	BOOL SendText(CIOCPContext *pContext,char *pText,int len);

protected:
	//申请和释放缓冲区对象
	CIOCPBuffer *AllocateBuffer(int len);
	void ReleaseBuffer(CIOCPBuffer *pBuffer);

	//申请和释放套接字上下文
	CIOCPContext *AllocateContext(SOCKET s);
	void ReleaseContext(CIOCPContext *pContext);

	//释放空闲缓冲区对象列表
	void FreeBuffers();

	//释放空闲上下文对象列表
	void FreeContexts();

	//向连接列表中添加一个连接
	BOOL AddAConnection(CIOCPContext *pContext);

	//插入和移除未决的请求
	BOOL InsertPendingAccept(CIOCPBuffer *pBuffer);
	BOOL RemovePendingAccept(CIOCPBuffer *pBuffer);

	//获取下一个要读取的对象
	CIOCPBuffer *GetNextReadBuffer(CIOCPContext *pContext, CIOCPBuffer *pBuffer);


	//投递 AcceptIO sendIO readIO
	BOOL PostAccept(CIOCPBuffer *pBuffer);
	BOOL PostSend(CIOCPContext *pContext, CIOCPBuffer *pBuffer);
	BOOL PostRecv(CIOCPContext *pContext, CIOCPBuffer *pBuffer);

	void HandleIO(CIOCPContext *pContext, CIOCPBuffer *pBuffer, DWORD dwTrans, int error);

	

	void _HandleWrite(CIOCPContext *pContext, CIOCPBuffer *pBuffer, DWORD dwTrans, int error);
	void _HandleRead(CIOCPContext *pContext, CIOCPBuffer *pBuffer, DWORD dwTrans, int error);
	void _HandleAccept(CIOCPContext *pContext, CIOCPBuffer *pBuffer, DWORD dwTrans, int error);

	void _HandleAcceptError(CIOCPBuffer * pBuffer);
	void _HandleReadWriteError(CIOCPContext *pContext,CIOCPBuffer *pBuffer,int error);

	void CloseBufferClient( CIOCPBuffer * pBuffer );
	void ReleaseContextAndBuffer( CIOCPContext * pContext, CIOCPBuffer * pBuffer );
	

	virtual void OnConnectionEstablished(CIOCPContext *pContext, CIOCPBuffer *pBuffer){}
	virtual void OnConnectionClosing(CIOCPContext *pContext, CIOCPBuffer *pBuffer){}
	virtual void OnConnectionError(CIOCPContext *pContext, CIOCPBuffer *pBuffer, int error){}
	virtual void OnReadCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer){}
	virtual void OnWriteCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer){}

private:
	void InitCfg();
	void InitHandle();
	void InitCriticalSection();
	void InitCount();
	void InitList();

private:
	static DWORD WINAPI _ListenThreadProc(LPVOID lpParam);
	static DWORD WINAPI _WorkerThreadProc(LPVOID lpParam);
	static const CInitSock sm_wsSocket;
private:
	CIOCPContext *m_pFreeContextList;
	CIOCPBuffer  *m_pFreeBufferList;
	int m_FreeBufferCount;
	int m_FreeContextCount;
	CRITICAL_SECTION m_FreeBufferListLock;
	CRITICAL_SECTION m_FreeContextListLock;

	CIOCPBuffer *m_pPendingAcceptList;
	long m_PendingAcceptCount;
	CRITICAL_SECTION m_PendingAcceptListLock;

	CIOCPContext *m_pConnectionList;
	int m_CurrentConnection;
	CRITICAL_SECTION m_ConnectionListLock;

	HANDLE m_hAcceptEvent;
	HANDLE m_hRepostEvent;
	LONG   m_ReportCount;

	int m_port;
	int m_InitialAccepts;
	int m_InitialReads;
	int m_MaxAccepts;
	int m_MaxSends;
	int m_MaxFreeBuffers;
	int m_MaxFreeContexts;
	int m_MaxConnections;

	HANDLE m_hListenThread;
	HANDLE m_hCompletion;
	SOCKET m_sListen;
	LPFN_ACCEPTEX m_lpfnAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS m_lpfnGetAcceptExSockaddrs;

	BOOL m_Shutdown;
	BOOL m_Started;
};