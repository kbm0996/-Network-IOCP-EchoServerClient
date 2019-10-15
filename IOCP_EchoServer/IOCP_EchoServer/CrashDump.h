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

		//pure virtual function called ���� �ڵ鷯�� ����� ���� �Լ��� ��ȸ��Ų��.
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

	// �ǵ����� Crash. Dump������ Crash �� �κ��� ã�� ���� Rapping�� �Լ�
	static void Crash()
	{
		int *p = nullptr;
		*p = 0;

	}

	static LONG WINAPI MyExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer)
	{
		int iWorkingMemory = 0;
		SYSTEMTIME stSysTime;

		//���� ���μ����� �޸� ��뷮�� ���´�.
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

		// C ��Ÿ�� ���̺귯�� ������ ���� �ڵ鷯 ����� ���� ���ؼ� API ��ŷ���.
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