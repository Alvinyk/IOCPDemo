#include "MyServerImp.hpp"


void main()
{
	CMyServer *pServer = new CMyServer;

	if (pServer->Start())
	{
		W2L(LV_INFO,"服务器开启成功。。。\n");
	}else
	{
		W2L(LV_INFO,"服务器开启失败。。。\n");
	}

	HANDLE hEvent = ::CreateEventA(NULL,FALSE,FALSE,"ShutdwonEvent");
	::WaitForSingleObject(hEvent,INFINITE);
	::CloseHandle(hEvent);
	pServer->Shutdown();
	delete pServer;

	W2L(LV_INFO,"服务器关闭 \n");
}