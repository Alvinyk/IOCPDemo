#pragma once
#include <string>
#include <io.h>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include "LogLevel.h"
using namespace std;


class CLogUtil
{
public:
	void static WriteLog2File(char* pszFile,int iLine,ETRAP_LOGLEVEL eLogLevel, char* pszFormat,...);
private:
	
	string static getCurDate();
	//删除间隔一个月的文件
	void static DeleteOldLogFile();
	void static Write2File(string content);
	
};

#define W2L(level,pszFmt,...)  	printf(pszFmt,##__VA_ARGS__);\
	CLogUtil::WriteLog2File(__FILE__,__LINE__,level,pszFmt,##__VA_ARGS__);