#include "CEchoDummy.h"

CEchoDummy::CEchoDummy()
{
	_lThreadLoopTps = 0;
	_lWaitEchoCnt = 0;
	_lLatencyMax = 0;

	_lError_DisconnectFromServerCnt = 0;

	//_lError_LoginPacket_Duplication = 0;
	//_lError_LoginPacket_NotRecv = 0;
	_lError_EchoNotRecv = 0;
	_lError_PacketErr = 0;
}

CEchoDummy::~CEchoDummy()
{

}

void CEchoDummy::PrintState()
{
	wprintf(L"===================================================\n");
	wprintf(L" Client:%d | UpdateThread:%d, WorkerThread:%d | Over Send:%d \n", _lConnectMax, _iUpdateThreadMax, _iWorkerThreadMax, _iOversendCnt);
	wprintf(L"===================================================\n");
	wprintf(L" Thread Loop : %lld \n", _lThreadLoopTps);
	wprintf(L" Wait Echo Count : %lld \n", _lWaitEchoCnt);
	wprintf(L" Max Latency : %lld ms \n", _lLatencyMax);
	wprintf(L"\n");
	wprintf(L" Connect Try : %lld \n", _lConnectTryTps);
	wprintf(L" Connect Success : %lld \n", _lConnectSuccessTps);
	wprintf(L" Connect Total : %lld \n", _lConnectCnt);
	wprintf(L" Connect Fail : %lld \n", _lConnectFailCnt);
	wprintf(L"\n");
	wprintf(L" Error - Disconnect from Server : %lld \n", _lError_DisconnectFromServerCnt);
	//wprintf(L" Error - LoginPacket Duplication : %lld \n", _lError_LoginPacket_Duplication);
	//wprintf(L" Error - LoginPacket Not Recv (3 sec) : %lld \n", _lError_LoginPacket_NotRecv);
	wprintf(L" Error - Echo Not Recv (500ms) : %lld \n", _lError_EchoNotRecv);
	wprintf(L" Error - Packet Error : %lld \n", _lError_PacketErr);
	wprintf(L"\n");
	wprintf(L" PacketPool Use	: %d \n", mylib::CNPacket::GetUseSize());	// 사용 중인 메모리풀 
	wprintf(L" SendPacket TPS	: %lld \n", _lSendTps);	/// WSASend를 호출할 때 애매함 - 보류, *접근 스레드가 다수이므로 interlocked 계열 함수 사용
	wprintf(L" RecvPacket TPS	: %lld \n", _lRecvTps);	// 초당 Recv 패킷 개수 - 완료통지 왔을때 Cnt, *접근 스레드가 다수이므로 interlocked 계열 함수 사용
	wprintf(L"\n");

	_lThreadLoopTps = 0;

	_lConnectTryTps = 0;
	_lConnectSuccessTps = 0;
	

	_lSendTps = 0;
	_lRecvTps = 0;
}

void CEchoDummy::OnClientJoin(UINT64 iSessionID)
{
}

void CEchoDummy::OnClientLeave(UINT64 iSessionID)
{
}

void CEchoDummy::OnRecv(UINT64 iSessionID, mylib::CNPacket * pPacket)
{
	//------------------------------------------------------------
	//	{
	//		UINT64	Precode
	//		ULONGLONG nData
	//	}
	//------------------------------------------------------------
	UINT64 lPrecode;
	ULONGLONG nData;

	*pPacket >> lPrecode;
	if (lPrecode != df_PACKET_PRECODE)
		return;

	*pPacket >> nData;

	ULONGLONG lCurRecvTick = GetTickCount64();
	stSESSION * pSession = &_arrSession[GetSessionIndex(iSessionID)];

	ULONGLONG nData_out;
	pSession->qEcho.Dequeue(nData_out);

	if (lCurRecvTick - pSession->lLastRecvTick > 500 )
		InterlockedIncrement64(&_lError_EchoNotRecv);

	if (nData_out != nData)
		InterlockedIncrement64(&_lError_PacketErr);

	if (pSession->lLastEcho == nData)
	{
		if (_bDisconnect && GetTickCount64() - pSession->lLastLoginTick > _iDisconnectDelay)
			pSession->lStatus = stSESSION::enSTAT::en_DISCONNECT;
	}

	pSession->lLastRecvTick = lCurRecvTick;
}

void CEchoDummy::OnSend(UINT64 iSessionID, int iSendSize)
{
}

void CEchoDummy::OnError(int iErrCode, WCHAR * wszErr)
{
}

void CEchoDummy::OnEcho(stSESSION * pSession)
{
	UINT64 lPrecode = df_PACKET_PRECODE;
	ULONGLONG nData;
	for (int i = 0; i < _iOversendCnt; ++i)
	{
		mylib::CNPacket * pOnPacket = mylib::CNPacket::Alloc();
		//------------------------------------------------------------
		//	WORD wHeader (payload size)
		//	{
		//		UINT64	Precode
		//		unsigned long long	nData
		//	}
		//------------------------------------------------------------
		nData = InterlockedIncrement64(&_nData);
		*pOnPacket << lPrecode;
		*pOnPacket << nData;

		pSession->qEcho.Enqueue(nData);

		SendPacket(pSession->iSessionID, pOnPacket);

		pOnPacket->Free();
	}

	pSession->lLastEcho = nData;
}
