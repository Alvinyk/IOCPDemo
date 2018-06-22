#pragma once
#include "CIOCPServer.h"
#include "../Common/log/LogUtil.h"

class CMyServer : public CIOCPServer
{
public :
	void OnConnectionEstablished(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
	{
		W2L(LV_INFO,"接收到一个新的连接 %d : %s\n",GetCurrentConnection(),::inet_ntoa(pContext->addrRemote.sin_addr));
		SendText(pContext,pBuffer->buff,pBuffer->nlen);
	}

	void OnConnectionClosing(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
	{
		W2L(LV_INFO," 一个连接关闭！ \n" );
	}

	void OnConnectionError(CIOCPContext *pContext, CIOCPBuffer *pBuffer, int nError)
	{
		W2L(LV_INFO," 一个连接发生错误： %d \n ", nError);
	}

	void OnReadCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
	{
		W2L(LV_INFO," 数据读取成功！\n ");
		SendText(pContext, pBuffer->buff, pBuffer->nlen);
	}

	void OnWriteCompleted(CIOCPContext *pContext, CIOCPBuffer *pBuffer)
	{
		W2L(LV_INFO," 数据发送成功！\n ");
	}
};