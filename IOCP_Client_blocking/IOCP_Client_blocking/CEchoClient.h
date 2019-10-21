#ifndef __NETWORK_H__
#define __NETWORK_H__

#include "_Define.h"
#include "CLanClient.h"
#include <list>

class CEchoClient : public mylib::CLanClient
{
public:
	CEchoClient();
	virtual ~CEchoClient();

	enum en_CHAT_LOG
	{
		en_CHATLINE_COUNT = 20,
		en_CHATLEN_MAX = 256
	};
	void InputChatline(WCHAR * szChat);
	void PrintChatline();

	void reqChat(WCHAR * szChat);
protected:
	void OnClientJoin();
	void OnClientLeave();
	void OnRecv(mylib::CNPacket * pPacket);
	void OnSend(int iSendSize);
	void OnError(int iErrCode, WCHAR * wszErr);

	
private:
	///WCHAR * _pChatLine[en_CHATLINE_COUNT];
	SRWLOCK _srwChatline;
	std::list<std::wstring> _Chatline;
};
#endif