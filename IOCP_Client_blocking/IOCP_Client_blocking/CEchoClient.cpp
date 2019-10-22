#include "CEchoClient.h"

CEchoClient::CEchoClient()
{
	InitializeSRWLock(reinterpret_cast<PSRWLOCK>(&_srwChatline));
}

CEchoClient::~CEchoClient()
{
}

void CEchoClient::InputChatline(WCHAR * szChat)
{
	std::wstring szInputChat = szChat;
	AcquireSRWLockExclusive(&_srwChatline);
	if (_Chatline.size() == en_CHATLINE_COUNT)
	{
		_Chatline.pop_front();
	}
	_Chatline.push_back(szInputChat);
	ReleaseSRWLockExclusive(&_srwChatline);
}
#include <iostream>
void CEchoClient::PrintChatline()
{
	int iNo = 0;
	AcquireSRWLockShared(&_srwChatline);
	for each(std::wstring sz in _Chatline)
		wprintf(L"%02d # %s\n", ++iNo, sz.c_str());
	ReleaseSRWLockShared(&_srwChatline);
}

void CEchoClient::reqChat(WCHAR * szChat)
{
	mylib::CNPacket * pOnPacket = mylib::CNPacket::Alloc();
	//------------------------------------------------------------
	//	WORD wHeader (payload size)
	//	{
	//		UINT64	Precode
	//		DWORD	Type
	//
	//		WORD	Size
	//		WCHAR	sz[Size]
	//	}
	//------------------------------------------------------------
	UINT64 lPrecode = df_PACKET_PRECODE;
	DWORD dwType = df_PACKET_TYPE_CHAT;
	WORD wSize = wcslen(szChat) * sizeof(WCHAR);

	*pOnPacket << lPrecode;
	*pOnPacket << dwType;
	*pOnPacket << wSize;
	pOnPacket->PutData(reinterpret_cast<char*>(szChat), wSize);

	SendPacket(pOnPacket);

	pOnPacket->Free();
}

void CEchoClient::OnClientJoin()
{
	reqChat(L"접속 완료 12");
}

void CEchoClient::OnClientLeave()
{
}

void CEchoClient::OnRecv(mylib::CNPacket * pPacket)
{
	//------------------------------------------------------------
	//	{
	//		UINT64	Precode
	//		DWORD	Type
	//
	//		WORD	Size
	//		WCHAR	sz[Size]
	//	}
	//------------------------------------------------------------
	UINT64 lPrecode;
	DWORD dwType;
	WORD wSize;
	WCHAR szMessage[en_CHATLEN_MAX];
		
	*pPacket >> lPrecode;
	if (lPrecode != df_PACKET_PRECODE)
		return;

	*pPacket >> dwType;
	if (dwType == df_PACKET_TYPE_CHAT)
	{
		*pPacket >> wSize;
		ZeroMemory(&szMessage, sizeof(szMessage));
		pPacket->GetData(reinterpret_cast<char*>(szMessage), wSize);


		//wprintf(L"%d %d %d %s\n", lPrecode, dwType, wSize, szMessage);

		//*pPacket << lPrecode;
		//*pPacket << dwType;
		//*pPacket << wSize;
		//pPacket->PutData(reinterpret_cast<char*>(szMessage), wSize);
		//wprintf(L"%d %d %d %s\n", lPrecode, dwType, wSize, szMessage);

		//*pPacket >> lPrecode;
		//*pPacket >> dwType;
		//*pPacket >> wSize;
		//pPacket->GetData(reinterpret_cast<char*>(szMessage), wSize);
		//wprintf(L"%d %d %d %s\n", lPrecode, dwType, wSize, szMessage);


		InputChatline(szMessage);
	}
}

void CEchoClient::OnSend(int iSendSize)
{
}

void CEchoClient::OnError(int iErrCode, WCHAR * wszErr)
{
}

