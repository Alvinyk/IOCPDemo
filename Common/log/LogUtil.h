#pragma once
#include <string>
#include <io.h>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <queue>
#include "LogLevel.h"
using namespace std;


class CLogUtil
{
public:
	void static WriteLog2File(char* pszFile,int iLine,ETRAP_LOGLEVEL eLogLevel, char* pszFormat,...);
	~CLogUtil();
private:
	static DWORD WINAPI WriteThreadProc(LPVOID lpParam);
private:
	string  getCurDate();
	//删除间隔一个月的文件
	void  DeleteOldLogFile();
	void  Write2File(string info);
	void  addLogInfo(string info);
	string getLogInfo();
private:
	CLogUtil();
	static CLogUtil* Instance;
	std::queue<string> logInfos;
	CRITICAL_SECTION m_FreeBufferListLock;
	HANDLE m_hWriteLogEvent;
	HANDLE m_hExitThreadEvent;
	string m_szOldDate;
	ofstream m_LogFile;
	bool m_bOpened;
};

#define W2L(level,pszFmt,...)  	printf(pszFmt,##__VA_ARGS__);\
	CLogUtil::WriteLog2File(__FILE__,__LINE__,level,pszFmt,##__VA_ARGS__);