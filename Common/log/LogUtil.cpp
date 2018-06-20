#include <windows.h>
#include "LogUtil.h"

void CLogUtil::WriteLog2File(char* pszFile,int iLine,ETRAP_LOGLEVEL eLogLevel, char* pszFormat,...)
{
	//�жϼ���
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

	//����log��Ϣͷ
	SYSTEMTIME st;
	GetLocalTime(&st);
	char chead[512] = {0};
	sprintf_s(chead,"[%04d-%02d-%02d %2d:%2d:%2d %d] FILE : %s  LINE : %d\n",
		st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,pszFile,iLine);

	//����log��Ϣ����
	string info;
	va_list argList;
	char* pszContent = new char[strlen(pszFormat)+ 1024];
	memset(pszContent,0,strlen(pszFormat)+ 1024);
	va_start(argList, pszFormat);
	_vsnprintf_s(pszContent, strlen(pszFormat)+ 1024,strlen(pszFormat)+ 1024, pszFormat, argList);
	va_end(argList);
	info.append(chead);
	info.append("Level:"+szLevel+"   ");
	info.append(pszContent);

	Write2File(info);
}

string CLogUtil::getCurDate()
{
	SYSTEMTIME stNow;
	char szDate[12] = {0};
	GetLocalTime(&stNow);
	sprintf_s(szDate,12,"%d-%d-%d", stNow.wYear,stNow.wMonth,stNow.wDay);
	return szDate;
}


//ɾ�����һ���µ��ļ�
void CLogUtil::DeleteOldLogFile()
{
	//��ȡ�ļ��б�
	//�����ļ����ں͵�ǰ���ڵĲ�ֵ
	//ɾ���������һ���µ��ļ�
}


void CLogUtil::Write2File(string content)
{
	//�ж�logĿ¼�Ƿ���ڣ��������򴴽�
	string PathName = "log/";
	if(-1 == _access(PathName.c_str(),0))
	{
		//Ŀ¼�����ڣ��½�Ŀ¼
		CreateDirectoryA(PathName.c_str(), 0); 
	}

	static FILE* pFile = nullptr;
	static string szOldDate;
	string szDate = getCurDate();
	static ofstream file;
	static bool bopen = false;

	//ͬһ�����־����ͬһ���ļ�
	if(szOldDate.compare(szDate) != 0 )
	{
		szOldDate = szDate;
		if(bopen){
			file.close();
			bopen = false;
		}
	}

	if (!bopen)
	{
		string szFile;
		szFile.append(PathName);
		szFile.append(szOldDate);
		szFile.append(".txt");
		file.open(szFile,ios::app);
		bopen = true;
	}
	if (bopen)
	{
		file<<content<<endl;
		file.flush();
	}
}