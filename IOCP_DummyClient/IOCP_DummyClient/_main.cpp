#include "CEchoClient.h"
#include <conio.h>
#include <iostream>

bool g_bMonitor;

CEchoClient g_LanServer;

bool ServerControl();

void main()
{
	_wsetlocale(LC_ALL, L"");
	LOG_SET(LOG_CONSOLE | LOG_FILE, LOG_DEBUG);
	if(!g_LanServer.Start(df_SERVER_IP, df_SERVER_PORT, 2, true))
		return;

	while (1)
	{
		system("cls");
		
		if(g_bMonitor)
			g_LanServer.PrintState(MON_CLIENT_ALL);
		
		g_LanServer.PrintChatline();

		if (!ServerControl())
			break;
	}

	g_LanServer.Stop();
}

bool ServerControl()
{
	static bool bControlMode = false;

	//------------------------------------------
	// L : Control Lock / U : Unlock / Q : Quit
	//------------------------------------------
	//  _kbhit() �Լ� ��ü�� ������ ������ ����� Ȥ�� ���̰� �������� �������� ���� �׽�Ʈ�� �ּ�ó�� ����
	// �׷����� GetAsyncKeyState�� �� �� ������ â�� Ȱ��ȭ���� �ʾƵ� Ű�� �ν���
	//  WinAPI��� ��� �����ϳ� �ֿܼ��� �����

	if (_kbhit())
	{
		WCHAR ControlKey = _getwch();

		if (L'u' == ControlKey || L'U' == ControlKey)
		{
			bControlMode = true;

			wprintf(L"[ Control Mode ] \n");
			wprintf(L"Press  L	- Key Lock \n");
			wprintf(L"Press  M	- Monitoring \n");
			wprintf(L"Press  Q	- Quit \n");
			
		}

		if (bControlMode == true)
		{
			if (L'l' == ControlKey || L'L' == ControlKey)
			{
				wprintf(L"Controll Lock. Press U - Control Unlock \n");
				bControlMode = false;
			}

			if (L'm' == ControlKey || L'M' == ControlKey)
			{
				wprintf(L"Controll Lock. Press U - Control Unlock \n");
				bControlMode = false;
			}

			if (L'q' == ControlKey || L'Q' == ControlKey)
			{
				return false;
			}
		}
	}

	return true;
}