#include <windows.h>
#include "LogUtil.h"

CLogUtil *CLogUtil::Instance = new CLogUtil();

CLogUtil::CLogUtil()
{
	m_bOpened = false;
	m_szOldDate = "";
	::InitializeCriticalSection(&m_FreeBufferListLock);
	m_hWriteLogEvent = ::CreateEvent(NULL,FALSE,FALSE,NULL);
	m_hExitThreadEvent = ::CreateEvent(NULL,FALSE,FALSE,NULL);
	::CreateThread(NULL,0,WriteThreadProc,this,0,NULL);
}

DWORD WINAPI CLogUtil::WriteThreadProc(LPVOID lpParam)
{
	CLogUtil *logUtil = (CLogUtil *)lpParam;
	HANDLE hWaitEvent[2] = {logUtil->m_hWriteLogEvent,logUtil->m_hExitThreadEvent};

	while (TRUE)
	{
		int result = ::WaitForMultipleObjects(2,hWaitEvent,FALSE,INFINITE);
		if (result == WAIT_OBJECT_0)
		{ 
			string info = logUtil->getLogInfo();
			while (info.empty())
			{
				logUtil->Write2File(info);
				info = logUtil->getLogInfo();
			}
			
		}else {
			::ExitThread(0);
		}
	}
}

CLogUtil::~CLogUtil()
{
	std::queue<string> empty;
	swap(empty,logInfos);
	SetEvent(m_hExitThreadEvent);
	if(m_bOpened){
		m_LogFile.close();
		m_bOpened = false;
	}
}

void CLogUtil::WriteLog2File(char* pszFile,int iLine,ETRAP_LOGLEVEL eLogLevel, char* pszFormat,...)
{
	//判断级别
	string szLevel;
	switch (eLogLevel)
	{
	case LV_INFO:
		szLevel="INFO";
		break;
	case LV_WARNING:
		szLevel="WARNING";
		break;
	case LV_ERROR:
		szLevel="ERROR";
		break;
	case LV_FATAL:
		szLevel="FATAL";
		break;
	default:       
		szLevel="INFO";
		break;
	}

	//构建log信息头
	SYSTEMTIME st;
	GetLocalTime(&st);
	char chead[512] = {0};
	sprintf_s(chead,"[%04d-%02d-%02d %2d:%2d:%2d %d] FILE : %s  LINE : %d\n",
		st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,pszFile,iLine);

	//构建log信息主体
	string info;
	va_list argList;
	int bufSize = 1024;
	char* pszContent = new char[strlen(pszFormat)+ bufSize];
	memset(pszContent,0,strlen(pszFormat)+ bufSize);
	va_start(argList, pszFormat);
	_vsnprintf_s(pszContent, strlen(pszFormat)+ bufSize,strlen(pszFormat)+ bufSize, pszFormat, argList);
	va_end(argList);
	info.append(chead);
	info.append("Level:"+szLevel+"   ");
	info.append(pszContent);

	Instance->addLogInfo(info);
}

string CLogUtil::getCurDate()
{
	SYSTEMTIME stNow;
	char szDate[12] = {0};
	GetLocalTime(&stNow);
	sprintf_s(szDate,12,"%d-%d-%d", stNow.wYear,stNow.wMonth,stNow.wDay);
	return szDate;
}


//删除间隔一个月的文件
void CLogUtil::DeleteOldLogFile()
{
	//读取文件列表
	//计算文件日期和当前日期的差值
	//删除超过间隔一个月的文件
}

void  CLogUtil::addLogInfo(string info)
{
	EnterCriticalSection(&m_FreeBufferListLock);
	logInfos.push(info);
	LeaveCriticalSection(&m_FreeBufferListLock);
	SetEvent(m_hWriteLogEvent);
}
string CLogUtil::getLogInfo()
{
	string info = "";
	EnterCriticalSection(&m_FreeBufferListLock);
	if(logInfos.empty() == false)
	{
		info = logInfos.front();
		logInfos.pop();
	}
	
	LeaveCriticalSection(&m_FreeBufferListLock);
	return info;
}

void CLogUtil::Write2File(string content)
{
	//判断log目录是否存在，不存在则创建
	string PathName = "log/";
	if(-1 == _access(PathName.c_str(),0))
	{
		//目录不存在，新建目录
		CreateDirectoryA(PathName.c_str(), 0); 
	}

	string szDate = getCurDate();
	//同一天的日志放入同一个文件
	if(m_szOldDate.compare(szDate) != 0 )
	{
		m_szOldDate = szDate;
		if(m_bOpened){
			m_LogFile.close();
			m_bOpened = false;
		}
	}

	if (!m_bOpened)
	{
		string szFile;
		szFile.append(PathName);
		szFile.append(m_szOldDate);
		szFile.append(".log");
		m_LogFile.open(szFile,ios::app);
		m_bOpened = true;
	}
	if (m_bOpened)
	{
		m_LogFile<<content<<endl;
		m_LogFile.flush();
	}
}