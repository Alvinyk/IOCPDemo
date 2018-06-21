#pragma once
#include "../Common/initsock2.h"
#include <Mswsock.h>

#define BUFFER_SIZE  1024*4 //IO���󻺳���
#define MAX_THREAD	 2		//IO�����߳�����

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

	//��ʼ����
	BOOL Start(char *pBindAddr, int port);
	
	//ֹͣ����
	void Shutdown();

	//�ر�һ�����Ӻ͹ر���������
	void CloseAConnection(CIOCPContext *pContext);
	void CloseAllConnections();

	//��ȡ��ǰ����������
	ULONG GetCurrentConnection();

	//��ָ���ͻ������ı�
	BOOL SendText(CIOCPContext *pContext,char *pText,int len);

protected:
	//������ͷŻ���������
	CIOCPBuffer *AllocateBuffer(int len);
	void ReleaseBuffer(CIOCPBuffer *pBuffer);

	//������ͷ��׽���������
	CIOCPContext *AllocateContext(SOCKET s);
	void ReleaseContext(CIOCPContext *pContext);

	//�ͷſ��л����������б�
	void FreeBuffers();

	//�ͷſ��������Ķ����б�
	void FreeContexts();

	//�������б������һ������
	BOOL AddAConnection(CIOCPContext *pContext);

	//������Ƴ�δ��������
	BOOL InsertPendingAccept(CIOCPBuffer *pBuffer);
	BOOL RemovePendingAccept(CIOCPBuffer *pBuffer);

	//��ȡ��һ��Ҫ��ȡ�Ķ���
	CIOCPBuffer *GetNextReadBuffer(CIOCPContext *pContext, CIOCPBuffer *pBuffer);


	//Ͷ�� AcceptIO sendIO readIO
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