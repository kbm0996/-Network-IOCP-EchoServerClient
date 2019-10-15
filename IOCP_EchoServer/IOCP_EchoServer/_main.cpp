#include "CEchoServer.h"
#include <conio.h>

CEchoServer LanServer;

bool ServerControl();

void main()
{
	LOG_SET(LOG_CONSOLE | LOG_FILE, LOG_DEBUG);
	LanServer.Start(df_SERVER_IP, df_SERVER_PORT, 4, false, 500);

	while (1)
	{
		system("cls");
		
		if (!ServerControl())
			break;

		LanServer.PrintState(MON_ALL);
		Sleep(1000);
	}

	LanServer.Stop();
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
			wprintf(L"Press  Q	- Quit \n");
		}

		if (bControlMode == true)
		{
			if (L'l' == ControlKey || L'L' == ControlKey)
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
