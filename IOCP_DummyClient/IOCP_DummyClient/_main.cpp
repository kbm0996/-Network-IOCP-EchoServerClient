#include "CEchoDummy.h"
#include <conio.h>
#include <iostream>
bool g_bMonitor;

CEchoDummy g_Dummy;

bool ServerControl();

void main()
{
	WCHAR szServerip[16] = { 0, };
	int bDisconnect;
	int iClientCnt;
	int iThreadCnt;
	int iOversendCnt;
	int iDisconnectDelay;
	int iLoopDelay;

	_wsetlocale(LC_ALL, L"");
	LOG_SET(LOG_CONSOLE | LOG_FILE, LOG_DEBUG);

	//wprintf(L"Server IP : ");
	//std::wcin.getline(szServerip, sizeof(szServerip));

	wprintf(L"Disconnect Test		1 = Yes / 2 = No : ");
	scanf_s("%d", &bDisconnect);
	switch (bDisconnect)
	{
	case 1:
		bDisconnect = TRUE;
		break;
	default:
	case 2:
		bDisconnect = FALSE;
		break;
	}

	wprintf(L"Client Count		1 = 1 / 2 = 2 / 3 = 50 / 4 = 100 [Best 50] : ");
	scanf_s("%d", &iClientCnt);
	switch (iClientCnt)
	{
	case 1:
		iClientCnt = 1;
		break;
	case 2:
		iClientCnt = 2;
		break;
	default:
	case 3:
		iClientCnt = 50;
		break;
	case 4:
		iClientCnt = 100;
		break;
	}

	wprintf(L"OverSend Count		1 = 1 / 2 = 100 / 3 = 200 [Best 100] : ");
	scanf_s("%d", &iOversendCnt);
	switch (iOversendCnt)
	{
	default:
	case 1:
		iOversendCnt = 1;
		break;
	case 2:
		iOversendCnt = 100;
		break;
	case 3:
		iOversendCnt = 200;
		break;
	}

	if (bDisconnect)
	{
		wprintf(L"Disconnect Delay	1 = 0 sec / 2 = 2 sec / 3 = 3 sec : ");
		scanf_s("%d", &iDisconnectDelay);
		switch (iDisconnectDelay)
		{
		default:
		case 1:
			iDisconnectDelay = 0;
			break;
		case 2:
			iDisconnectDelay = 2000;
			break;
		case 3:
			iDisconnectDelay = 3000;
			break;
		}
	}
	if (bDisconnect && iClientCnt >= 50)
		iThreadCnt = iClientCnt / 2;
	else if (!bDisconnect && iClientCnt >= 50)
		iThreadCnt = iClientCnt / 50;
	else
		iThreadCnt = 1;

	wprintf(L"Loop Delay ms [Best 0] : ");
	scanf_s("%d", &iLoopDelay);

	if(!g_Dummy.Start(df_SERVER_IP, df_SERVER_PORT, true, iClientCnt, iThreadCnt, bDisconnect, iOversendCnt, iDisconnectDelay, iLoopDelay))
		return;

	while (1)
	{	
		if (!ServerControl())
			break;

		g_Dummy.PrintState();

		Sleep(1000);
	}

	g_Dummy.Stop();
}

bool ServerControl()
{
	if (!g_Dummy._bSendEcho)
		wprintf(L" S : Echo PLAY | Q : Quit\n");
	else
		wprintf(L" S : Echo STOP | Q : Quit\n");
	if (!g_Dummy._bDisconnect)
		wprintf(L" C : Reconnect PLAY\n");
	else
		wprintf(L" C : Reconnect STOP\n");

	if (_kbhit())
	{
		WCHAR ControlKey = _getwch();

		if (L's' == ControlKey || L'S' == ControlKey)
			g_Dummy._bSendEcho = !g_Dummy._bSendEcho;

		if (L'c' == ControlKey || L'C' == ControlKey)
			g_Dummy._bDisconnect = !g_Dummy._bDisconnect;

		if (L'q' == ControlKey || L'Q' == ControlKey)
			return false;

	}

	return true;
}