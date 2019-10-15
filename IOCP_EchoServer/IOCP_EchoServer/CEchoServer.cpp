#include "CEchoServer.h"


CEchoServer::CEchoServer()
{
}

CEchoServer::~CEchoServer()
{
}

bool CEchoServer::OnConnectRequest(WCHAR * wszIP, int iPort)
{
	return true;
}

void CEchoServer::OnClientJoin(UINT64 SessionID)
{
	mylib::CNPacket *pOnPacket = mylib::CNPacket::Alloc();

	WORD wHeader = sizeof(UINT64);
	UINT64 lData = df_PACKET_PRECODE;
	*pOnPacket << lData;
	pOnPacket->SetHeader_Custom((char*)&wHeader, sizeof(wHeader));

	SendPacket(SessionID, pOnPacket);

	pOnPacket->Free();
}

void CEchoServer::OnClientLeave(UINT64 SessionID)
{
}

void CEchoServer::OnRecv(UINT64 SessionID, mylib::CNPacket * pPacket)
{
	mylib::CNPacket *pSendPacket = mylib::CNPacket::Alloc();

	WORD wHeader = pPacket->GetDataSize();
	pSendPacket->PutData(pPacket->GetPayloadPtr(), wHeader);
	pSendPacket->SetHeader_Custom((char*)&wHeader, sizeof(wHeader));

	SendPacket(SessionID, pSendPacket);

	pSendPacket->Free();
}

void CEchoServer::OnSend(UINT64 SessionID, int iSendSize)
{
}

void CEchoServer::OnError(int iErrCode, WCHAR * wszErr)
{
}
