#ifndef __CCrashDump_LIB__
#define __CCrashDump_LIB__

#include<Windows.h>
#include<stdio.h>
#include<crtdbg.h>
#include<signal.h>
#include<DbgHelp.h>
#include<Psapi.h>
#include"APIHook.h"

class CCrashDump
{
private:
	CCrashDump()
	{
		_invalid_parameter_handler oldHandler, newHandler;
		newHandler = myInvalidParameterHandler;
		oldHandler = _set_invalid_parameter_handler(newHandler);

		_CrtSetReportHook(_custom_Report_hook);
		_CrtSetReportMode(_CRT_WARN, 0);
		_CrtSetReportMode(_CRT_ASSERT, 0);
		_CrtSetReportMode(_CRT_ERROR, 0);

		//pure virtual function called 에러 핸들러를 사용자 정의 함수로 우회시킨다.
		_set_purecall_handler(myPurecallHandler);

		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		signal(SIGABRT, signalHandler);
		signal(SIGINT, signalHandler);
		signal(SIGILL, signalHandler);
		signal(SIGFPE, signalHandler);
		signal(SIGSEGV, signalHandler);
		signal(SIGTERM, signalHandler);

		SetHandlerDump();
	}

public:
	static CCrashDump *GetInstance()
	{
		static CCrashDump Inst;
		return &Inst;
	}

	// 의도적인 Crash. Dump내려고 Crash 낸 부분을 찾기 위해 Rapping한 함수
	static void Crash()
	{
		int *p = nullptr;
		*p = 0;

	}

	static LONG WINAPI MyExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer)
	{
		int iWorkingMemory = 0;
		SYSTEMTIME stSysTime;

		//현재 프로세스의 메모리 사용량을 얻어온다.
		HANDLE hProcess = 0;
		PROCESS_MEMORY_COUNTERS pmc;

		hProcess = GetCurrentProcess();
		if (hProcess == 0)
			return 0;

		if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
		{
			iWorkingMemory = (int)(pmc.WorkingSetSize / 1024 / 1024);
		}
		CloseHandle(hProcess);

		WCHAR filename[MAX_PATH];
		GetLocalTime(&stSysTime);
		wsprintf(filename, L"Dump_%d-%02d-%02d %02d.%02d.%02d.dmp", stSysTime.wYear, stSysTime.wMonth, stSysTime.wDay, stSysTime.wHour, stSysTime.wMinute, stSysTime.wSecond);
		wprintf(L"!!Crash Error!!\n%d-%d-%d %d:%d:%d\n", stSysTime.wYear, stSysTime.wMonth, stSysTime.wDay, stSysTime.wHour, stSysTime.wMinute, stSysTime.wSecond);

		HANDLE hDumpFile = ::CreateFileW(filename, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hDumpFile != INVALID_HANDLE_VALUE)
		{
			_MINIDUMP_EXCEPTION_INFORMATION MinidumpExceptionImformation;

			MinidumpExceptionImformation.ThreadId = ::GetCurrentThreadId();
			MinidumpExceptionImformation.ExceptionPointers = pExceptionPointer;
			MinidumpExceptionImformation.ClientPointers = TRUE;

			MiniDumpWriteDump(
				GetCurrentProcess(),
				GetCurrentProcessId(),
				hDumpFile,
				MiniDumpWithFullMemory,
				&MinidumpExceptionImformation,
				NULL, NULL);

			CloseHandle(hDumpFile);
			wprintf(L"Dump Save Ok\n");
		}
		return EXCEPTION_EXECUTE_HANDLER;
	}

	static LONG WINAPI RedirectedSetUnhandledExceptionFilter(EXCEPTION_POINTERS *exceptionInfo)
	{
		MyExceptionFilter(exceptionInfo);
		return EXCEPTION_EXECUTE_HANDLER;
	}


	void SetHandlerDump()
	{
		SetUnhandledExceptionFilter(MyExceptionFilter);

		// C 런타임 라이브러리 내부의 예외 핸들러 등록을 막기 위해서 API 후킹사용.
		static CAPIHook apiHook("kernel32.dll", "SetUnhandledExceptionFilter", (PROC)RedirectedSetUnhandledExceptionFilter, true);
	}


	// Invalid Parameter handler
	static void myInvalidParameterHandler(const wchar_t *expression, const wchar_t *function, const wchar_t *filfile, unsigned int line, uintptr_t pReserved)
	{
		Crash();
	}

	static int _custom_Report_hook(int ireposttype, char *message, int *returnvalue)
	{
		Crash();
		return true;
	}

	static void myPurecallHandler()
	{
		Crash();
	}

	static void signalHandler(int Error)
	{
		Crash();
	}
};

#define CRASH CCrashDump::GetInstance()->Crash

#endif