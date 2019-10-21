#include "CEchoClient.h"
#include <conio.h>
#include <iostream>

CEchoClient g_LanServer;

bool ServerControl();
bool ServerControl(std::wstring szInput);

void main()
{
	_wsetlocale(LC_ALL, L"");
	LOG_SET(LOG_CONSOLE | LOG_FILE, LOG_DEBUG);
	if(!g_LanServer.Start(df_SERVER_IP, df_SERVER_PORT, 2, true))
		return;

	WCHAR szInput[CEchoClient::en_CHATLEN_MAX];
	while (1)
	{
		system("cls");
		//g_LanServer.PrintState(MON_CLIENT_ALL);
		
		g_LanServer.PrintChatline();

		std::wcin.getline(szInput, g_LanServer.en_CHATLEN_MAX);
		if (!ServerControl(szInput))
			break;

		g_LanServer.reqChat(szInput);
	}

	g_LanServer.Stop();
}

bool ServerControl()
{
	static bool bControlMode = false;

	//------------------------------------------
	// L : Control Lock / U : Unlock / Q : Quit
	//------------------------------------------
	//  _kbhit() 함수 자체가 느리기 때문에 사용자 혹은 더미가 많아지면 느려져서 실제 테스트시 주석처리 권장
	// 그런데도 GetAsyncKeyState를 안 쓴 이유는 창이 활성화되지 않아도 키를 인식함
	//  WinAPI라면 제어가 가능하나 콘솔에선 어려움

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

			if (L'q' == ControlKey || L'Q' == ControlKey)
			{
				return false;
			}
		}
	}

	return true;
}

bool ServerControl(std::wstring szInput)
{
	if (szInput == L"q")
		return false;

	return true;
}