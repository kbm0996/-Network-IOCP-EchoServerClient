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

	UINT64 lData = df_PACKET_PRECODE;
	*pOnPacket << lData;

	SendPacket(SessionID, pOnPacket);

	pOnPacket->Free();
}

void CEchoServer::OnClientLeave(UINT64 SessionID)
{
}

void CEchoServer::OnRecv(UINT64 SessionID, mylib::CNPacket * pPacket)
{
	mylib::CNPacket *pSendPacket = mylib::CNPacket::Alloc();

	pSendPacket->PutData(pPacket->GetPayloadPtr(), pPacket->GetDataSize());

	//LONGLONG szData;
	//memcpy(&szData, pPacket->GetPayloadPtr(), pPacket->GetDataSize());
	//printf("%d\n", szData);

	SendPacket(SessionID, pSendPacket);

	pSendPacket->Free();
}

void CEchoServer::OnSend(UINT64 SessionID, int iSendSize)
{
}

void CEchoServer::OnError(int iErrCode, WCHAR * wszErr)
{
}
