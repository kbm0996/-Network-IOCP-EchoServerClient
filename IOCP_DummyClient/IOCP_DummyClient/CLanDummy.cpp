#include "CLanDummy.h"

mylib::CLanDummy::CLanDummy()
{
	_bServerOn = FALSE;

	_iOversendCnt = 0;
	_iDisconnectDelay = 0;
	_iSessionID = 0;

	_lThreadLoopTps = 0;
	_lConnectTryTps = 0;
	_lConnectSuccessTps = 0;
	_lConnectCnt = 0;
	_lConnectFailCnt = 0;

	_lRecvTps = 0;
	_lSendTps = 0;


	_nData = 0;
}

mylib::CLanDummy::~CLanDummy()
{
}

bool mylib::CLanDummy::Start(WCHAR * wszConnectIP, int iPort, bool bNagle, __int64 iConnectMax, int iWorkerThreadCnt, bool bDisconnect, int iOversendCnt, int iDisconnectDelay, int iLoopDelay)
{
	memcpy(_szServerIP, wszConnectIP, sizeof(_szServerIP));
	_iPort = iPort;
	_bNagle = bNagle;
	_iWorkerThreadMax = 4;// iWorkerThreadCnt;
	_iUpdateThreadMax = iWorkerThreadCnt;//iUpdateThreadCnt;
	_lConnectMax = iConnectMax;
	_iOversendCnt = iOversendCnt;
	_bSendEcho = true;
	_iLoopDelay = iLoopDelay;
	_bDisconnect = bDisconnect;
	_iDisconnectDelay = iDisconnectDelay;
	
	// IOCP init
	_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	if (_hIOCP == NULL)
	{
		LOG(L"SYSTEM", LOG_ERROR, L"CreateIoCompletionPort() failed %d", WSAGetLastError());
		return false;
	}

	// Winsock init
	WSADATA wsa;
	if (WSAStartup(WINSOCK_VERSION, &wsa) != 0)
	{
		LOG(L"SYSTEM", LOG_ERROR, L"WSAStartup() failed %d", WSAGetLastError());
		CloseHandle(_hIOCP);
		return false;
	}

	_serveraddr.sin_family = AF_INET;
	_serveraddr.sin_port = htons(_iPort);
	InetPton(AF_INET, _szServerIP, reinterpret_cast<PVOID>(&_serveraddr.sin_addr));

	// Session Init
	_arrSession = new stSESSION[iConnectMax];
	for (int i = iConnectMax - 1; i >= 0; --i)
	{
		_arrSession[i].nIndex = i;
		///_arrSession[i].iSessionID = CreateSessionID(++_iSessionID, i);
		_stkSession.Push(i);
	}

	// Thread start
	_hWorkerThread = new HANDLE[_iWorkerThreadMax];
	for (int i = 0; i < _iWorkerThreadMax; ++i)
		_hWorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, NULL);

	_bServerOn = TRUE;

	_arrThreadInfo = new stTHREAD_INFO[_iUpdateThreadMax];
	for (int i = 0; i < _iUpdateThreadMax; ++i)
	{
		_arrThreadInfo[i].pThisClass = this;
		_arrThreadInfo[i].iSessionMax = 0;
	}
	for (int i = 0; i < _lConnectMax; ++i)
		++_arrThreadInfo[i%_iUpdateThreadMax].iSessionMax;

	_hUpdateThread = new HANDLE[_iUpdateThreadMax];
	for (int i = 0; i < _iUpdateThreadMax; ++i)
		_hUpdateThread[i] = (HANDLE)_beginthreadex(NULL, 0, UpdateThread, reinterpret_cast<VOID*>(&_arrThreadInfo[i]), 0, NULL);
	
	LOG(L"SYSTEM", LOG_SYSTM, L"Start");
	return true;
}

void mylib::CLanDummy::Stop()
{
	_bServerOn = FALSE;

	// Thread end
	WaitForMultipleObjects(_iUpdateThreadMax, _hUpdateThread, TRUE, INFINITE);
	for (int i = 0; i < _iUpdateThreadMax; ++i)
		CloseHandle(_hUpdateThread[i]);

	for (int i = 0; i < _iWorkerThreadMax; ++i)
		PostQueuedCompletionStatus(_hIOCP, 0, 0, NULL); // 스레드 종료를 위한 IOCP 패킷을 생성하여 IOCP에 전송

	WaitForMultipleObjects(_iWorkerThreadMax, _hWorkerThread, TRUE, INFINITE);
	for (int i = 0; i < _iWorkerThreadMax; ++i)
		CloseHandle(_hWorkerThread[i]);
	
	delete[] _arrThreadInfo;

	// Winsock closse
	WSACleanup();

	// IOCP close
	CloseHandle(_hIOCP);

	// Session Release
	for (int i = 0; i < _lConnectMax; ++i)
	{
		if (_arrSession[i].Socket != INVALID_SOCKET)
			ReleaseSession(&_arrSession[i]);
	}

	if (_arrSession != nullptr)
		delete[] _arrSession;
	
	if (_hWorkerThread != nullptr)
		delete[] _hWorkerThread;
	
	_lRecvTps = 0;
	_lSendTps = 0;

	LOG(L"SYSTEM", LOG_DEBUG, L"Client Stop");
}

mylib::CLanDummy::stSESSION * mylib::CLanDummy::ConnectSession(int nIndex)
{
	stSESSION * pSession = &_arrSession[nIndex];
	pSession->lStatus = stSESSION::enSTAT::en_CONNECT;

	InterlockedIncrement64(&_lConnectTryTps);

	pSession->Socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (pSession->Socket == INVALID_SOCKET)
	{
		LOG(L"SYSTEM", LOG_ERROR, L"socket() failed %d", WSAGetLastError());
		return false;
	}
	
	linger optval;
	optval.l_onoff = 1;
	optval.l_linger = 0;
	setsockopt(pSession->Socket, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
	setsockopt(pSession->Socket, IPPROTO_TCP, TCP_NODELAY, (char *)&_bNagle, sizeof(_bNagle));

	// Server connect
	if (connect(pSession->Socket, reinterpret_cast<sockaddr*>(&_serveraddr), sizeof(_serveraddr)) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		// WSAENOTSOCK(10038) : 소켓이 아닌 항목에 소켓 작업 시도
		// WSAECONNABORTED(10053) : 호스트가 연결 중지. 데이터 전송 시간 초과, 프로토콜 오류 발생
		// WSAECONNRESET(10054) : 원격 호스트에 의해 기존 연결 강제 해제. 원격 호스트가 갑자기 중지되거나 다시 시작되거나 하드 종료를 사용하는 경우
		// WSAESHUTDOWN(10058) : 소켓 종료 후 전송
		// WSAECONNREFUSED(10061) : 서버 연결 거부
		if (err != 10038 && err != 10053 && err != 10054 && err != 10058 && err != 10061)
		{
			// WSAEINVAL(10022) : 바인딩 실패. 이미 bind된 소켓에 바인드하거나 주소체계가 일관적이지 않을 때
			// WSAEISCONN(10056) : 이미 연결이 완료된 소켓임. connect로 연결이 완료된 소켓에 다시 connect를 시도할 경우
			LOG(L"SYSTEM", LOG_ERROR, L"connect() failed %d", err);
		}

		return nullptr;
	}

	InterlockedIncrement64(&_lConnectCnt);

	InterlockedIncrement64(&pSession->stIO->iCnt);

	pSession->iSessionID = CreateSessionID(++_iSessionID, nIndex);
	pSession->RecvQ.Clear();
	pSession->SendQ.Clear();
	pSession->bSendFlag = FALSE;
	pSession->iSendPacketCnt = 0;
	pSession->bSendDisconnect = FALSE;

	pSession->lLastLoginTick = pSession->lLastRecvTick = GetTickCount64();
	pSession->stkEcho.Clear();

	if (CreateIoCompletionPort((HANDLE)pSession->Socket, _hIOCP, (ULONG_PTR)pSession, 0) == NULL)
	{
		//  HANDLE 인자에 소켓이 아닌 값이 올 경우 잘못된 핸들(6번 에러) 발생. 
		// 소켓이 아닌 값을 넣었다는 것은 다른 스레드에서 소켓을 반환했다는 의미이므로  동기화 문제일 가능성이 높다.
		LOG(L"SYSTEM", LOG_ERROR, L"Session IOCP Enrollment failed %d", WSAGetLastError());
		if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
			ReleaseSession(pSession);
		return nullptr;
	}

	pSession->stIO->bRelease = FALSE;

	OnClientJoin(pSession->iSessionID);

	// SessionSocket을 recv 상태로 변경
	RecvPost(pSession);

	// Accept하면서 Accept 패킷을 보내면서 IOCount가 1이 증가하므로 다시 1 감소
	if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
	{
		ReleaseSession(pSession);
		return nullptr;
	}

	return pSession;
}

void mylib::CLanDummy::DisconnectSession(SOCKET socket)
{
	//shutdown(socket, SD_BOTH);
	closesocket(socket);
}

bool mylib::CLanDummy::SendPacket(UINT64 iSessionID, CNPacket * pPacket)
{
	stSESSION *pSession = ReleaseSessionLock(iSessionID);
	if (pSession == nullptr)
		return false;

	pPacket->AddRef();
	WORD wHeader = pPacket->GetDataSize();
	pPacket->SetHeader_Custom((char*)&wHeader, sizeof(wHeader));

	pSession->SendQ.Enqueue(pPacket);
	SendPost(pSession);

	ReleaseSessionFree(pSession);
	return true;
}

bool mylib::CLanDummy::SendPacket_Disconnect(UINT64 iSessionID, CNPacket * pPacket)
{
	stSESSION *pSession = ReleaseSessionLock(iSessionID);
	if (pSession == nullptr)
		return false;

	pPacket->AddRef();
	WORD wHeader = pPacket->GetDataSize();
	pPacket->SetHeader_Custom((char*)&wHeader, sizeof(wHeader));

	pSession->SendQ.Enqueue(pPacket);
	SendPost(pSession);

	pSession->bSendDisconnect = true;

	ReleaseSessionFree(pSession);
	return true;
}

mylib::CLanDummy::stSESSION * mylib::CLanDummy::ReleaseSessionLock(UINT64 iSessionID)
{
	int iIndex = GetSessionIndex(iSessionID);
	//  TODO : Multi-Thread 환경에서 Release, Connect, Send, Accept 등이 ContextSwitching으로 인해 동시 발생할 수 있음을 감안해야 함
	stSESSION *pSession = &_arrSession[iIndex];

	/************************************************************/
	/* 각종 세션 상태에서 다른 스레드가 Release를 시도했을 경우 */
	/************************************************************/
	// **CASE 0 : Release 플래그가 TRUE로 확실히 바뀐 상태
	if (pSession->stIO->bRelease == TRUE)
		return nullptr;

	// **CASE 1 : ReleaseSession 내에 있을 경우
	// 이때 다른 스레드에서 SendPacket이나 Disconnect 시도 시, IOCnt가 1이 될 수 있음
	if (InterlockedIncrement64(&pSession->stIO->iCnt) == 1)
	{
		// 검증하면서 증가시킨 IOCnt를 다시 감소시켜 복구
		if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
			ReleaseSession(pSession); // Release를 중첩으로 하는 문제는 Release 내에서 처리
		return nullptr;
	}

	// **CASE 2 : 이미 Disconnect 된 다음 Accept하여 새로 Session이 들어왔을 경우
	if (pSession->iSessionID != iSessionID) // 현 SessionID와 ContextSwitching 발생 이전의 SessionID를 비교
	{
		if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
			ReleaseSession(pSession);
		return nullptr;
	}

	// **CASE 3 : Release 플래그가 FALSE임이 확실한 상태
	if (pSession->stIO->bRelease == FALSE)
		return pSession;

	// **CASE 4 : CASE 3에 진입하기 직전에 TRUE로 바뀌었을 경우
	if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
		ReleaseSession(pSession);
	return nullptr;
}

void mylib::CLanDummy::ReleaseSessionFree(stSESSION * pSession)
{
	if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
		ReleaseSession(pSession);
	return;
}

bool mylib::CLanDummy::ReleaseSession(stSESSION * pSession)
{
	stIOREF stComparentIO(0, FALSE);
	if (!InterlockedCompareExchange128((LONG64*)pSession->stIO, TRUE, 0, (LONG64*)&stComparentIO))
		return false;

	pSession->lStatus = stSESSION::enSTAT::en_RELEASE;

	// TODO : shutdown()
	//  shutdown() 함수는 4HandShake(1Fin - 2Ack+3Fin - 4Fin)에서 1Fin을 보내는 함수
	// 1. 상대편에서 이에 대한 Ack를 보내지 않으면(대게 상대방이 '응답없음'으로 먹통됐을 경우) 종료가 되지 않음
	//   - 이 현상(연결 끊어야하는데 안끊어지는 경우)이 지속되면 메모리가 순간적으로 증가하고 SendBuffer가 터질 수 있음
	// 2. TimeWait이 남음
	//   - CancelIoEx 사용하여 4HandShake를 무시하고 닫아야 남지 않는다
	DisconnectSession(pSession->Socket);

	// TODO : CancelIoEx()
	//  shutdown으로는 닫을 수 없을 경우 사용 (SendBuffer가 가득 찼을때, Heartbeat가 끊어졌을때)
	// CancelIoEx()과 WSARecv() 간에 경합이 발생한다. 경합이 발생하더라도 shutdown()이 WSARecv()를 실패하도록 유도한다.
	CancelIoEx(reinterpret_cast<HANDLE>(pSession->Socket), NULL);

	OnClientLeave(pSession->iSessionID);

	CNPacket *pPacket = nullptr;
	while (pSession->SendQ.Dequeue(pPacket))
	{
		if (pPacket != nullptr)
			pPacket->Free();
		pPacket = nullptr;
	}

	InterlockedExchange(&pSession->bSendFlag, FALSE);
	pSession->iSessionID = -1;
	pSession->Socket = INVALID_SOCKET;
	pSession->stkEcho.Clear();
	_stkSession.Push(pSession->nIndex);
	InterlockedDecrement64(&_lConnectCnt);

	return true;
}

bool mylib::CLanDummy::RecvPost(stSESSION * pSession)
{
	WSABUF wsabuf[2];
	int iBufCnt = 1;
	wsabuf[0].buf = pSession->RecvQ.GetWriteBufferPtr();
	wsabuf[0].len = pSession->RecvQ.GetUnbrokenEnqueueSize();
	if (pSession->RecvQ.GetUnbrokenEnqueueSize() < pSession->RecvQ.GetFreeSize())
	{
		wsabuf[1].buf = pSession->RecvQ.GetBufferPtr();
		wsabuf[1].len = pSession->RecvQ.GetFreeSize() - wsabuf[0].len;
		++iBufCnt;
	}

	DWORD dwTransferred = 0;
	DWORD dwFlag = 0;
	ZeroMemory(&pSession->RecvOverlapped, sizeof(pSession->RecvOverlapped));
	InterlockedIncrement64(&pSession->stIO->iCnt);
	if (WSARecv(pSession->Socket, wsabuf, iBufCnt, &dwTransferred, &dwFlag, &pSession->RecvOverlapped, NULL) == SOCKET_ERROR) // 성공 : 0, 실패 : SOCKET_ERROR
	{
		int err = WSAGetLastError();
		// WSA_IO_PENDING(997) : 비동기 입출력 중. Overlapped 연산은 나중에 완료될 것이다. 중첩 연산을 위한 준비가 되었으나, 즉시 완료되지 않았을 경우 발생
		if (err != WSA_IO_PENDING)
		{
			// WSAENOTSOCK(10038) : 소켓이 아닌 항목에 소켓 작업 시도
			// WSAECONNABORTED(10053) : 호스트가 연결 중지. 데이터 전송 시간 초과, 프로토콜 오류 발생
			// WSAECONNRESET(10054) : 원격 호스트에 의해 기존 연결 강제 해제. 원격 호스트가 갑자기 중지되거나 다시 시작되거나 하드 종료를 사용하는 경우
			// WSAESHUTDOWN(10058) : 소켓 종료 후 전송
			if (err != 10038 && err != 10053 && err != 10054 && err != 10058)
			{
				// WSAEINTR(10004) : 블록화 호출 취소. 데이터 수신이나 송신상에서 버퍼초과나 일정시간동안  기다렸는데도 수신을 할수 없는 경우
				LOG(L"SYSTEM", LOG_ERROR, L"WSARecv() failed %d / Socket:%d / RecvQ Size:%d", err, pSession->Socket, pSession->RecvQ.GetUseSize());
			}
			DisconnectSession(pSession->Socket);
			if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
				ReleaseSession(pSession);
			return false;
		}
	}
	return true;
}

bool mylib::CLanDummy::SendPost(stSESSION * pSession)
{
	///* Recursive Func Version *///
	//if (pSession->SendQ.GetUseSize() == 0)
	//		return false;
	//if(InterlockedCompareExchange(&pSession->bSendFlag, TRUE, FALSE) == TRUE)
	//	return false;
	//if (pSession->SendQ.GetUseSize() == 0)
	//{
	//	SendPost(pSession);
	//	return false;
	//}

	///* goto Version *///
	//{
	//Loop:
	//	if (pSession->SendQ.GetUseSize() == 0)
	//		return false;
	//	if (InterlockedCompareExchange(&pSession->bSendFlag, TRUE, FALSE) == TRUE)
	//		return false;
	//	if (pSession->SendQ.GetUseSize() == 0)
	//	{
	//		InterlockedExchange(&pSession->bSendFlag, FALSE);
	//		goto Loop;
	//	}
	//}

	///* Loop Version *///
	while (1)
	{
		if (pSession->SendQ.GetUseSize() == 0)
			return false;
		if (InterlockedCompareExchange(&pSession->bSendFlag, TRUE, FALSE) == TRUE)
			return false;
		if (pSession->SendQ.GetUseSize() == 0)
		{
			InterlockedExchange(&pSession->bSendFlag, FALSE);
			continue;
		}
		break;
	}

	int iBufCnt;
	WSABUF wsabuf[100];
	CNPacket * pPacket;
	int iPacketCnt = pSession->SendQ.GetUseSize();
	for (iBufCnt = 0; iBufCnt < iPacketCnt && iBufCnt < 100; ++iBufCnt)
	{
		if (!pSession->SendQ.Peek(pPacket, iBufCnt))
			break;
		wsabuf[iBufCnt].buf = pPacket->GetHeaderPtr();
		wsabuf[iBufCnt].len = pPacket->GetPacketSize();
	}
	pSession->iSendPacketCnt = iBufCnt;

	DWORD dwTransferred;
	ZeroMemory(&pSession->SendOverlapped, sizeof(pSession->SendOverlapped));
	InterlockedIncrement64(&pSession->stIO->iCnt);
	if (WSASend(pSession->Socket, wsabuf, iBufCnt, &dwTransferred, 0, &pSession->SendOverlapped, NULL) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		// WSA_IO_PENDING(997) : 비동기 입출력 중. Overlapped 연산은 나중에 완료될 것이다. 중첩 연산을 위한 준비가 되었으나, 즉시 완료되지 않았을 경우 발생
		if (err != ERROR_IO_PENDING)
		{
			// WSAENOTSOCK(10038) : 소켓이 아닌 항목에 소켓 작업 시도
			// WSAECONNABORTED(10053) : 호스트가 연결 중지. 데이터 전송 시간 초과, 프로토콜 오류 발생
			// WSAECONNRESET(10054) : 원격 호스트에 의해 기존 연결 강제 해제. 원격 호스트가 갑자기 중지되거나 다시 시작되거나 하드 종료를 사용하는 경우
			// WSAESHUTDOWN(10058) : 소켓 종료 후 전송
			if (err != 10038 && err != 10053 && err != 10054 && err != 10058)
				LOG(L"SYSTEM", LOG_ERROR, L"WSASend() # failed %d / Socket:%d / IOCnt:%d / SendQ Size:%d", err, pSession->Socket, pSession->stIO->iCnt, pSession->SendQ.GetUseSize());

			InterlockedExchange(&pSession->bSendFlag, FALSE);
			DisconnectSession(pSession->Socket);
			if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
				ReleaseSession(pSession);

			return false;
		}
	}
	return true;
}

void mylib::CLanDummy::RecvComplete(stSESSION * pSession, DWORD dwTransferred)
{
	// 수신량만큼 RecvQ.MoveWritePos 이동
	pSession->RecvQ.MoveWritePos(dwTransferred);

	// Header는 Payload 길이를 담은 2Byte 크기의 타입
	WORD wHeader;
	while (1)
	{
		// 1. RecvQ에 'sizeof(Header)'만큼 있는지 확인
		int iRecvSize = pSession->RecvQ.GetUseSize();
		if (iRecvSize < sizeof(wHeader))
		{
			// 더 이상 처리할 패킷이 없음
			// 만약, Header 크기보다 작은 모종의 데이터가 누적된다면 2번에서 걸러짐
			break;
		}

		// 2. Packet 길이 확인 : Header크기('sizeof(Header)') + Payload길이('Header')
		pSession->RecvQ.Peek(reinterpret_cast<char*>(&wHeader), sizeof(wHeader));
		if (iRecvSize < sizeof(wHeader) + wHeader)
		{
			// Header의 Payload의 길이가 실제와 다름
			DisconnectSession(pSession->Socket);
			LOG(L"SYSTEM", LOG_WARNG, L"Header & PayloadLength mismatch");
			break;
		}
		// RecvQ에서 Packet의 Header 부분 제거
		pSession->RecvQ.MoveReadPos(sizeof(wHeader));

		CNPacket *pPacket = CNPacket::Alloc();

		// 3. Payload 길이 확인 : PacketBuffer의 최대 크기보다 Payload가 클 경우
		if (pPacket->GetBufferSize() < wHeader)
		{
			pPacket->Free();
			DisconnectSession(pSession->Socket);
			LOG(L"SYSTEM", LOG_WARNG, L"PacketBufferSize < PayloadSize ");
			break;
		}

		// 4. PacketPool에 Packet 포인터 할당
		if (pSession->RecvQ.Dequeue(pPacket->GetPayloadPtr(), wHeader) != wHeader)
		{
			pPacket->Free();
			LOG(L"SYSTEM", LOG_WARNG, L"RecvQ dequeue error");
			DisconnectSession(pSession->Socket);
			break;
		}

		// RecvQ에서 Packet의 Payload 부분 제거
		pPacket->MoveWritePos(wHeader);

		InterlockedIncrement64(&_lRecvTps);

		// 5. Packet 처리
		OnRecv(pSession->iSessionID, pPacket);
		pPacket->Free();
	}

	// SessionSocket을 recv 상태로 변경
	RecvPost(pSession);
}

void mylib::CLanDummy::SendComplete(stSESSION * pSession, DWORD dwTransferred)
{
	OnSend(pSession->iSessionID, dwTransferred);

	// 보낸 패킷 수 만큼 지우기
	CNPacket *pPacket;
	for (int i = 0; i < pSession->iSendPacketCnt; ++i)
	{
		pPacket = nullptr;
		if (pSession->SendQ.Dequeue(pPacket))
		{
			pPacket->Free();
			InterlockedIncrement64(&_lSendTps);
		}
	}

	if (pSession->bSendDisconnect == TRUE)
		DisconnectSession(pSession->Socket);

	InterlockedExchange(&pSession->bSendFlag, FALSE);
	SendPost(pSession);
}

unsigned int mylib::CLanDummy::MonitorThread(LPVOID pCLanDummy)
{
	return ((CLanDummy *)pCLanDummy)->MonitorThread_Process();
}

unsigned int mylib::CLanDummy::UpdateThread(LPVOID pThreadInfo)
{
	CLanDummy * pCLanDummy = ((stTHREAD_INFO *)pThreadInfo)->pThisClass;
	return pCLanDummy->UpdateThread_Process(((stTHREAD_INFO *)pThreadInfo)->iSessionMax);
}


unsigned int mylib::CLanDummy::WorkerThread(LPVOID pCLanDummy)
{
	return ((CLanDummy *)pCLanDummy)->WorkerThread_Process();
}

unsigned int mylib::CLanDummy::MonitorThread_Process()
{
	return 0;
}

unsigned int mylib::CLanDummy::UpdateThread_Process(int iSessionMax)
{
	std::vector<stSESSION*> vtSession;
	vtSession.clear();

	while (_bServerOn)
	{
		// Connect
		if (vtSession.size() < iSessionMax)
		{
			int nIndex = -1;
			stSESSION * pSession;
			if (_stkSession.Pop(nIndex))
			{
				pSession = ConnectSession(nIndex);
				if (pSession != nullptr)
				{
					InterlockedIncrement64(&_lConnectSuccessTps);
					vtSession.push_back(pSession);
					pSession->lStatus = stSESSION::enSTAT::en_SEND;
				}
				else
				{
					InterlockedIncrement64(&_lConnectFailCnt);
					_stkSession.Push(nIndex);
				}
			}
		}

		// Send
		if (_bSendEcho)
		{
			for (auto iter = vtSession.begin(); iter != vtSession.end(); /*++iter*/)
			{
				stSESSION * pSession = *iter;
				if (!pSession->stkEcho.GetUseSize())
				{
					if (pSession->lStatus == stSESSION::enSTAT::en_SEND)
					{
						OnEcho(pSession);
						//if (_bDisconnect && GetTickCount64() - pSession->lLastLoginTick >= _iDisconnectDelay)
						//	pSession->lStatus = stSESSION::enSTAT::en_DISCONNECT;
					}
				}
				++iter;
			}
		}
		
		// Disconnect
		if (_bDisconnect)
		{
			for (auto iter = vtSession.begin(); iter != vtSession.end();)
			{
				stSESSION * pSession = *iter;
				if (!pSession->stkEcho.GetUseSize())
				{
					if (pSession->lStatus == stSESSION::enSTAT::en_DISCONNECT)
					{
						DisconnectSession(pSession->Socket);
						iter = vtSession.erase(iter);
						continue;
					}
				}
				++iter;
			}
		}

		InterlockedIncrement64(&_lThreadLoopTps);
		Sleep(_iLoopDelay);
	}

	return 0;
}

unsigned int mylib::CLanDummy::WorkerThread_Process()
{
	DWORD		 dwTransferred;
	stSESSION *	 pSession;
	LPOVERLAPPED pOverlapped;
	while (1)
	{
		// GQCS 실패시 이전 값을 그대로 반환 → 인자값 초기화 필수
		dwTransferred = 0;
		pSession = 0;
		pOverlapped = 0;
		/*----------------------------------------------------------------------------------------------------------------
		// GQCS 리턴값 //

		(1) IOCP Queue로부터 완료 패킷 dequeue 성공일 경우.
		-> TRUE 리턴, lpCompletionKey 세팅

		(2) IOCP Queue로부터 완료 패킷 dequeue 실패
		&& lpOverlapped가 NULL(0)일 경우.
		-> FALSE 리턴

		(3) IOCP Queue로부터 완료 패킷 dequeue 성공
		&& lpOverlapped가 NOT_NULL
		&& dequeue한 요소(메세지)가 실패한 I/O일 경우.
		-> FALSE 리턴, lpCompletionKey 세팅

		(4) IOCP에 등록된 Socket이 close됐을 경우.
		-> FALSE 리턴, lpOverlapped에 NOT_NULL, lpNumberOfBytes에 0 세팅
		----------------------------------------------------------------------------------------------------------------*/
		BOOL bResult = GetQueuedCompletionStatus(_hIOCP, &dwTransferred, reinterpret_cast<PULONG_PTR>(&pSession), &pOverlapped, INFINITE);
		if (pOverlapped == 0)
		{
			// (2)
			if (bResult == FALSE)
			{
				LOG(L"SYSTEM", LOG_ERROR, L"GetQueuedCompletionStatus() failed %d", GetLastError());
				PostQueuedCompletionStatus(_hIOCP, 0, 0, 0);	// GQCS 에러 시 조치를 취할만한게 없으므로 종료
				break;
			}

			// (x) 정상 종료 (PQCS로 지정한 임의 메세지)
			if (dwTransferred == 0 && pSession == 0)
			{
				PostQueuedCompletionStatus(_hIOCP, 0, 0, 0);
				break;
			}
		}

		// (3)(4) 세션 종료
		if (dwTransferred == 0)
		{
			DisconnectSession(pSession->Socket);
		}
		// (1)
		else
		{
			// * GQCS Send/Recv 완료 통지 처리
			//  완료 통지 : TCP/IP suite가 관리하는 버퍼에 복사 성공
			if (pOverlapped == &pSession->RecvOverlapped)
				RecvComplete(pSession, dwTransferred);

			if (pOverlapped == &pSession->SendOverlapped)
				SendComplete(pSession, dwTransferred);
		}

		if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
			ReleaseSession(pSession);
	}
	LOG(L"SYSTEM", LOG_DEBUG, L"WorkerThread Exit");
	return 0;
}

mylib::CLanDummy::stSESSION::stSESSION()
{
	iSessionID = -1;
	Socket = INVALID_SOCKET;

	stIO = (stIOREF*)_aligned_malloc(sizeof(stIOREF), 16);

	stIO->iCnt = 0;
	stIO->bRelease = FALSE;

	RecvQ.Clear();
	SendQ.Clear();
	bSendFlag = FALSE;
	iSendPacketCnt = 0;
	bSendDisconnect = FALSE;

	lLastLoginTick = 0;
	lLastRecvTick = 0;
	lStatus = enSTAT::en_CONNECT;
}

mylib::CLanDummy::stSESSION::~stSESSION()
{
	_aligned_free(stIO);
}