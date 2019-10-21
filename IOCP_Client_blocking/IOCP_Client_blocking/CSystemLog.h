/*---------------------------------------------------------------
  Log
  로그 클래스. 
 파일 및 콘솔에 단계별(DEBUG, WARNG, ERROR, SYSTEM) 로그 출력 
 - 사용법
 LOG_SET(LOG_CONSOLE | LOG_FILE, LOG_DEBUG, L"MMOSERVER_LOG");
 LOG(L"SYSTEM", LOG_ERROR, L"Error() StringCchVPrintf() Error");
----------------------------------------------------------------*/
#ifndef __SYSTEM_LOG__
#define __SYSTEM_LOG__

#include <stdio.h>
#include <tchar.h>
#include <Strsafe.h>
#include <Windows.h>

namespace mylib
{
	class CSystemLog
	{
	public:
		enum en_SYSTEM_LOG
		{
			// 출력 방식 (비트 연산)
			en_CONSOLE = 1,
			en_FILE = 2,

			// 로그 레벨
			LEVEL_DEBUG = 100,
			LEVEL_WARNG = 101,
			LEVEL_ERROR = 102,
			LEVEL_SYSTM = 103
		};

	private:
		CSystemLog()
		{
			InitializeSRWLock(&_srwLock);
			_iWriteFlag = en_CONSOLE;
			_iLogLevel = LEVEL_DEBUG;
			_lLogCnt = 0;
			_wmkdir(L"_LOG");
		}

		~CSystemLog() {}

	public:
		static CSystemLog *GetInstance()
		{
			static CSystemLog Ins;
			return &Ins;
		}

	public:
		// 기본 폴더(_LOG) 설정
		void LOG_SET(int iWriteFlag, int iLogLevel)
		{
			_iWriteFlag = iWriteFlag;
			_iLogLevel = iLogLevel;
			wsprintf(_szDir_Subname, L"_LOG");
		}

		// 하위 폴더(_LOG\\szDir_SubName) 설정
		void LOG_SET(int iWriteFlag, int iLogLevel, WCHAR *szDir_SubName)
		{
			_iWriteFlag = iWriteFlag;
			_iLogLevel = iLogLevel;
			wsprintf(_szDir_Subname, L"_LOG\\%s", szDir_SubName);
			_wmkdir(_szDir_Subname);
		}

		void LOG_SET_DIR(WCHAR *szDir)
		{
			wsprintf(_szDir, L"%s\\%s", _szDir_Subname, szDir);
			_wmkdir(_szDir);
		}

		void LOG(WCHAR *szType, int iLogLevel, WCHAR *szString, ...)
		{
			// 설정된 `로그 레벨` 이하의 모든 로그 출력
			if (iLogLevel < _iLogLevel)
				return;

			// 로그 횟수
			long lLogCount = InterlockedIncrement(&_lLogCnt);

			// `로그 레벨` 문자열
			WCHAR szLogLevel[6];
			switch (iLogLevel)
			{
			case LEVEL_DEBUG:
				wsprintf(szLogLevel, L"DEBUG");
				break;
			case LEVEL_WARNG:
				wsprintf(szLogLevel, L"WARNG");
				break;
			case LEVEL_ERROR:
				wsprintf(szLogLevel, L"ERROR");
				break;
			case LEVEL_SYSTM:
				wsprintf(szLogLevel, L"SYSTM");
				break;
			}

			// 가변인자 처리
			WCHAR szLogString[1024] = { 0, };
			va_list vaList;
			va_start(vaList, szString);
			HRESULT hResult = StringCchVPrintf(szLogString, sizeof(szLogString), szString, vaList);
			va_end(vaList);
			if (FAILED(hResult))
			{
				szLogString[1023] = '\0';
				LOG(szType, LEVEL_WARNG, L"Too Long Log");
			}

			SYSTEMTIME stSysTime;
			GetLocalTime(&stSysTime);

			// WriteFlag를 확인하여 flag에 해당하는 것만 출력
			if (_iWriteFlag & en_CONSOLE)
			{
				// 콘솔에 로그 출력
				wprintf(L"[%s] [%02d:%02d:%02d / %s] %s\n", szType, stSysTime.wHour, stSysTime.wMinute, stSysTime.wSecond, szLogLevel, szLogString);
			}

			if (_iWriteFlag & en_FILE)
			{
				// 파일에 로그 저장
				// 폴더
				LOG_SET_DIR(szType);

				// 파일명
				WCHAR szFilename[MAX_PATH];
				wsprintf(szFilename, L"%s\\%s_%d%02d.txt", _szDir, szType, stSysTime.wYear, stSysTime.wMonth);

				// 로그
				WCHAR szLog[2048] = { 0, };
				wsprintf(szLog, L"[%s] [%d-%02d-%02d %02d:%02d:%02d / %s / %08d] %s\n", szType, stSysTime.wYear, stSysTime.wMonth, stSysTime.wDay,
					stSysTime.wHour, stSysTime.wMinute, stSysTime.wSecond, szLogLevel, lLogCount, szLogString);

				AcquireSRWLockExclusive(&_srwLock);
				FILE *fLog;
				do
				{
					_wfopen_s(&fLog, szFilename, L"a");
				} while (fLog == NULL);

				fputws(szLog, fLog);
				fclose(fLog);
				ReleaseSRWLockExclusive(&_srwLock);
			}
		}

	private:
		int		_iWriteFlag;		// 출력 형태
		int		_iLogLevel;			// 출력 로그 레벨
		long	_lLogCnt;			// 로그 횟수
		WCHAR	_szDir[64];			// 로그 저장 주소
		WCHAR	_szDir_Subname[64];	
		SRWLOCK _srwLock;
	};

#define LOG			mylib::CSystemLog::GetInstance()->LOG
#define LOG_SET		mylib::CSystemLog::GetInstance()->LOG_SET
#define LOG_SET_DIR	mylib::CSystemLog::GetInstance()->LOG_SET_DIR

#define LOG_CONSOLE	mylib::CSystemLog::en_CONSOLE
#define LOG_FILE	mylib::CSystemLog::en_FILE

#define LOG_DEBUG	mylib::CSystemLog::LEVEL_DEBUG
#define LOG_WARNG	mylib::CSystemLog::LEVEL_WARNG
#define LOG_ERROR	mylib::CSystemLog::LEVEL_ERROR
#define LOG_SYSTM	mylib::CSystemLog::LEVEL_SYSTM
}
#endif