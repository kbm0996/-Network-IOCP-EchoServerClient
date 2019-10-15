#ifndef __NETWORK_H__
#define __NETWORK_H__

#include "_Define.h"
#include "CLanServer.h"

class CEchoServer : public mylib::CLanServer
{
public:
	CEchoServer();
	virtual ~CEchoServer();

protected:
	bool OnConnectRequest(WCHAR* wszIP, int iPort);
	void OnClientJoin(UINT64 SessionID);
	void OnClientLeave(UINT64 SessionID);
	void OnRecv(UINT64 SessionID, mylib::CNPacket * pPacket);
	void OnSend(UINT64 SessionID, int iSendSize);
	void OnError(int iErrCode, WCHAR * wszErr);
};
#endif