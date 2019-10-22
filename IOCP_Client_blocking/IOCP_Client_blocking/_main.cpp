#include "CEchoClient.h"
#include <conio.h>
#include <iostream>

CEchoClient g_LanServer;

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
		g_LanServer.PrintState(MON_CLIENT_ALL);
		
		g_LanServer.PrintChatline();

		std::wcin.getline(szInput, g_LanServer.en_CHATLEN_MAX);
		if (!ServerControl(szInput))
			break;

		g_LanServer.reqChat(szInput);
	}

	g_LanServer.Stop();
}

bool ServerControl(std::wstring szInput)
{
	if (szInput == L"q")
		return false;

	return true;
}