#include "CLanClient.h"

mylib::CLanClient::CLanClient()
{
	_pSession = 0;
	_bConnect = 0;
	_lRecvTps = 0;
	_lSendTps = 0;
}

mylib::CLanClient::~CLanClient()
{
	if (_pSession != nullptr)
		delete[] _pSession;
}

bool mylib::CLanClient::Start(WCHAR * wszConnectIP, int iPort, int iWorkerThreadCnt, bool bNagle)
{
	if (_pSession != nullptr)
		return false;

	_iPort = iPort;
	_iWorkerThreadMax = iWorkerThreadCnt;
	memcpy(_szServerIP, wszConnectIP, sizeof(_szServerIP));

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
		return false;
	}

	_pSession = new stSESSION();
	_pSession->Socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (_pSession->Socket == INVALID_SOCKET)
	{
		LOG(L"SYSTEM", LOG_ERROR, L"socket() failed %d", WSAGetLastError());
		return false;
	}

	setsockopt(_pSession->Socket, IPPROTO_TCP, TCP_NODELAY, (char *)&bNagle, sizeof(bNagle));

	// Server connect
	SOCKADDR_IN serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(iPort);
	InetPton(AF_INET, wszConnectIP, reinterpret_cast<PVOID>(&serveraddr.sin_addr));
	if (connect(_pSession->Socket, reinterpret_cast<sockaddr*>(&serveraddr), sizeof(serveraddr)) == SOCKET_ERROR)
	{
		LOG(L"SYSTEM", LOG_ERROR, L"connect() failed %d", WSAGetLastError());
		return false;
	}

	// SOL_SOCKET - SO_SNDBUF
	//  소켓의 송신 버퍼 크기를 0으로 만들면 소켓 버퍼를 건너뛰고 프로토콜 스택의 송신 버퍼에 다이렉트로 전달되어 성능 향상.
	// 단, 수신 버퍼의 크기는 건들지 않음. 도착지를 잃고 수신 파트는 프로그램 구조상 서버측에서 제어하는게 아니라
	// 클라가 주는대로 받아야 하므로 패킷 처리 속도가 수신 속도를 따라오지 못할 수 있음.
	///int iBufSize = 0;
	///int iOptLen = sizeof(iBufSize);
	///setsockopt(_pSession->Socket, SOL_SOCKET, SO_SNDBUF, (char *)&iBufSize, iOptLen);

	// Thread start
	for (int i = 0; i < iWorkerThreadCnt; ++i)
		_hWorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, NULL);

	if (CreateIoCompletionPort((HANDLE)_pSession->Socket, _hIOCP, (ULONG_PTR)_pSession, 0) == NULL)
	{
		//  HANDLE 인자에 소켓이 아닌 값이 올 경우 잘못된 핸들(6번 에러) 발생. 
		// 소켓이 아닌 값을 넣었다는 것은 다른 스레드에서 소켓을 반환했다는 의미이므로  동기화 문제일 가능성이 높다.
		int err = WSAGetLastError();
		LOG(L"SYSTEM", LOG_ERROR, L"Session IOCP Enrollment ErrorCode:%d", err);
		if (InterlockedDecrement64(&_pSession->stIO->iCnt) == 0)
			ReleaseSession();
		return false;
	}

	InterlockedIncrement64(&_pSession->stIO->iCnt);

	OnClientJoin();

	// SessionSocket을 recv 상태로 변경
	RecvPost();

	// Accept하면서 Accept 패킷을 보내면서 IOCount가 1이 증가하므로 다시 1 감소
	if (InterlockedDecrement64(&_pSession->stIO->iCnt) == 0)
		ReleaseSession();

	LOG(L"SYSTEM", LOG_SYSTM, L"Client Start");

	_bConnect = TRUE;
	return true;
}

void mylib::CLanClient::Stop(bool bReconnect)
{
	_bConnect = FALSE;

	// Server disconnect
	closesocket(_pSession->Socket);
	

	// Thread end
	if (!bReconnect)
	{
		for (int i = 0; i < _iWorkerThreadMax; ++i)
			PostQueuedCompletionStatus(_hIOCP, 0, 0, NULL); // 스레드 종료를 위한 IOCP 패킷을 생성하여 IOCP에 전송
		WaitForMultipleObjects(_iWorkerThreadMax, _hWorkerThread, TRUE, INFINITE);
		for (int i = 0; i < _iWorkerThreadMax; ++i)
			CloseHandle(_hWorkerThread[i]);

		// Winsock closse
		WSACleanup();

		// IOCP close
		CloseHandle(_hIOCP);
	}

	// Session release
	ReleaseSession();
	if (_pSession != nullptr)
	{
		delete _pSession;
		_pSession = nullptr;
	}

	_lRecvTps = 0;
	_lSendTps = 0;

	LOG(L"SYSTEM", LOG_DEBUG, L"Client Stop");
}

void mylib::CLanClient::PrintState(int iFlag)
{
	if (iFlag & en_ALL)
	{
		wprintf(L"===========================================\n");
		wprintf(L" Lan Client\n");
		wprintf(L"===========================================\n");
		if (iFlag & en_PACKET_TPS)
		{
			wprintf(L" - RecvPacket TPS	: %lld \n", _lRecvTps);	// 초당 Recv 패킷 개수 - 완료통지 왔을때 Cnt, *접근 스레드가 다수이므로 interlocked 계열 함수 사용
			wprintf(L" - SendPacket TPS	: %lld \n", _lSendTps);	/// WSASend를 호출할 때 애매함 - 보류, *접근 스레드가 다수이므로 interlocked 계열 함수 사용
			_lRecvTps = 0;
			_lSendTps = 0;
		}
		if (iFlag & en_PACKETPOOL_SIZE)
			wprintf(L" - PacketPool Use	: %d \n", CNPacket::GetUseSize());	// 사용 중인 메모리풀 	
	}
}

bool mylib::CLanClient::SendPacket(CNPacket * pPacket)
{
	pPacket->AddRef();
	WORD wHeader = pPacket->GetDataSize();
	pPacket->SetHeader_Custom((char*)&wHeader, sizeof(wHeader));

	_pSession->SendQ.Enqueue(pPacket);
	SendPost();
	return true;
}

bool mylib::CLanClient::ReleaseSession()
{
	// TODO : shutdown()
	//  shutdown() 함수는 4HandShake(1Fin - 2Ack+3Fin - 4Fin)에서 1Fin을 보내는 함수
	// 1. 상대편에서 이에 대한 Ack를 보내지 않으면(대게 상대방이 '응답없음'으로 먹통됐을 경우) 종료가 되지 않음
	//   - 이 현상(연결 끊어야하는데 안끊어지는 경우)이 지속되면 메모리가 순간적으로 증가하고 SendBuffer가 터질 수 있음
	// 2. TimeWait이 남음
	//   - CancelIoEx 사용하여 4HandShake를 무시하고 닫아야 남지 않는다
	shutdown(_pSession->Socket, SD_BOTH);

	// TODO : CancelIoEx()
	//  shutdown으로는 닫을 수 없을 경우 사용 (SendBuffer가 가득 찼을때, Heartbeat가 끊어졌을때)
	// CancelIoEx()과 WSARecv() 간에 경합이 발생한다. 경합이 발생하더라도 shutdown()이 WSARecv()를 실패하도록 유도한다.
	CancelIoEx(reinterpret_cast<HANDLE>(_pSession->Socket), NULL);

	OnClientLeave();

	CNPacket *pPacket = nullptr;
	while (_pSession->SendQ.Dequeue(pPacket))
	{
		if (pPacket != nullptr)
			pPacket->Free();
		pPacket = nullptr;
	}

	_pSession->iSessionID = -1;
	_pSession->Socket = INVALID_SOCKET;
	return true;
}

bool mylib::CLanClient::RecvPost()
{
	WSABUF wsabuf[2];
	int iBufCnt = 1;
	wsabuf[0].buf = _pSession->RecvQ.GetWriteBufferPtr();
	wsabuf[0].len = _pSession->RecvQ.GetUnbrokenEnqueueSize();
	if (_pSession->RecvQ.GetUnbrokenEnqueueSize() < _pSession->RecvQ.GetFreeSize())
	{
		wsabuf[1].buf = _pSession->RecvQ.GetBufferPtr();
		wsabuf[1].len = _pSession->RecvQ.GetFreeSize() - wsabuf[0].len;
		++iBufCnt;
	}

	DWORD dwTransferred = 0;
	DWORD dwFlag = 0;
	ZeroMemory(&_pSession->RecvOverlapped, sizeof(_pSession->RecvOverlapped));
	InterlockedIncrement64(&_pSession->stIO->iCnt);
	if (WSARecv(_pSession->Socket, wsabuf, iBufCnt, &dwTransferred, &dwFlag, &_pSession->RecvOverlapped, NULL) == SOCKET_ERROR) // 성공 : 0, 실패 : SOCKET_ERROR
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
				LOG(L"SYSTEM", LOG_ERROR, L"WSARecv() ErrorCode:%d / Socket:%d / RecvQ Size:%d", err, _pSession->Socket, _pSession->RecvQ.GetUseSize());
			
			shutdown(_pSession->Socket, SD_BOTH);
			if (InterlockedDecrement64(&_pSession->stIO->iCnt) == 0)
				ReleaseSession();
			return false;
		}
	}
	return true;
}

bool mylib::CLanClient::SendPost()
{
	///* Recursive Func Version *///
	//if (_pSession->SendQ.GetUseSize() == 0)
	//		return false;
	//if(InterlockedCompareExchange(&_pSession->bSendFlag, TRUE, FALSE) == TRUE)
	//	return false;
	//if (_pSession->SendQ.GetUseSize() == 0)
	//{
	//	SendPost(_pSession);
	//	return false;
	//}

	///* goto Version *///
	//{
	//Loop:
	//	if (_pSession->SendQ.GetUseSize() == 0)
	//		return false;
	//	if (InterlockedCompareExchange(&pSession->bSendFlag, TRUE, FALSE) == TRUE)
	//		return false;
	//	if (_pSession->SendQ.GetUseSize() == 0)
	//	{
	//		InterlockedExchange(&_pSession->bSendFlag, FALSE);
	//		goto Loop;
	//	}
	//}

	///* Loop Version *///
	while (1)
	{
		if (_pSession->SendQ.GetUseSize() == 0)
			return false;
		if (InterlockedCompareExchange(&_pSession->bSendFlag, TRUE, FALSE) == TRUE)
			return false;
		if (_pSession->SendQ.GetUseSize() == 0)
		{
			InterlockedExchange(&_pSession->bSendFlag, FALSE);
			continue;
		}
		break;
	}

	int iBufCnt;
	WSABUF wsabuf[100];
	CNPacket * pPacket;
	int iPacketCnt = _pSession->SendQ.GetUseSize();
	for (iBufCnt = 0; iBufCnt < iPacketCnt && iBufCnt < 100; ++iBufCnt)
	{
		if (!_pSession->SendQ.Peek(pPacket, iBufCnt))
			break;
		wsabuf[iBufCnt].buf = pPacket->GetHeaderPtr();
		wsabuf[iBufCnt].len = pPacket->GetPacketSize();
	}
	_pSession->iSendPacketCnt = iBufCnt;

	DWORD dwTransferred;
	ZeroMemory(&_pSession->SendOverlapped, sizeof(_pSession->SendOverlapped));
	InterlockedIncrement64(&_pSession->stIO->iCnt);
	if (WSASend(_pSession->Socket, wsabuf, iBufCnt, &dwTransferred, 0, &_pSession->SendOverlapped, NULL) == SOCKET_ERROR)
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
				LOG(L"SYSTEM", LOG_ERROR, L"WSASend() # ErrorCode:%d / Socket:%d / IOCnt:%d / SendQ Size:%d", err, _pSession->Socket, _pSession->stIO->iCnt, _pSession->SendQ.GetUseSize());
			
			//InterlockedExchange(&_pSession->bSendFlag, FALSE);
			shutdown(_pSession->Socket, SD_BOTH);
			if (InterlockedDecrement64(&_pSession->stIO->iCnt) == 0)
				ReleaseSession();

			return false;
		}
	}
	return true;
}

void mylib::CLanClient::RecvComplete(DWORD dwTransferred)
{
	// 수신량만큼 RecvQ.MoveWritePos 이동
	_pSession->RecvQ.MoveWritePos(dwTransferred);

	// Header는 Payload 길이를 담은 2Byte 크기의 타입
	WORD wHeader;
	while (1)
	{
		// 1. RecvQ에 'sizeof(Header)'만큼 있는지 확인
		int iRecvSize = _pSession->RecvQ.GetUseSize();
		if (iRecvSize < sizeof(wHeader))
		{
			// 더 이상 처리할 패킷이 없음
			// 만약, Header 크기보다 작은 모종의 데이터가 누적된다면 2번에서 걸러짐
			break;
		}

		// 2. Packet 길이 확인 : Header크기('sizeof(Header)') + Payload길이('Header')
		_pSession->RecvQ.Peek(reinterpret_cast<char*>(&wHeader), sizeof(wHeader));
		if (iRecvSize < sizeof(wHeader) + wHeader)
		{
			// Header의 Payload의 길이가 실제와 다름
			shutdown(_pSession->Socket, SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"Header & PayloadLength mismatch");
			break;
		}
		// RecvQ에서 Packet의 Header 부분 제거
		_pSession->RecvQ.MoveReadPos(sizeof(wHeader));

		CNPacket *pPacket = CNPacket::Alloc();

		// 3. Payload 길이 확인 : PacketBuffer의 최대 크기보다 Payload가 클 경우
		if (pPacket->GetBufferSize() < wHeader)
		{
			pPacket->Free();
			shutdown(_pSession->Socket, SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"PacketBufferSize < PayloadSize ");
			break;
		}

		// 4. PacketPool에 Packet 포인터 할당
		if (_pSession->RecvQ.Dequeue(pPacket->GetPayloadPtr(), wHeader) != wHeader) 
		{
			pPacket->Free();
			LOG(L"SYSTEM", LOG_WARNG, L"RecvQ dequeue error");
			shutdown(_pSession->Socket, SD_BOTH);
			break;
		}

		// RecvQ에서 Packet의 Payload 부분 제거
		pPacket->MoveWritePos(wHeader);

		++_lRecvTps;

		// 5. Packet 처리
		OnRecv(pPacket);
		pPacket->Free();
	}

	// SessionSocket을 recv 상태로 변경
	RecvPost();
}

void mylib::CLanClient::SendComplete(DWORD dwTransferred)
{
	OnSend(dwTransferred);

	// 보낸 패킷 수 만큼 지우기
	CNPacket *pPacket;
	for (int i = 0; i < _pSession->iSendPacketCnt; ++i)
	{
		pPacket = nullptr;
		if (_pSession->SendQ.Dequeue(pPacket))
		{
			pPacket->Free();
			++_lSendTps;
		}
	}

	InterlockedExchange(&_pSession->bSendFlag, FALSE);
	SendPost();
}

unsigned int mylib::CLanClient::MonitorThread(LPVOID pCLanClient)
{
	return ((CLanClient *)pCLanClient)->MonitorThread_Process();
}

unsigned int mylib::CLanClient::WorkerThread(LPVOID pCLanClient)
{
	return ((CLanClient *)pCLanClient)->WorkerThread_Process();
}

unsigned int mylib::CLanClient::MonitorThread_Process()
{
	return 0;
}

unsigned int mylib::CLanClient::WorkerThread_Process()
{
	DWORD			dwTransferred;
	stSESSION *		pSession;
	LPOVERLAPPED	pOverlapped;
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
				LOG(L"SYSTEM", LOG_ERROR, L"GetQueuedCompletionStatus() ErrorCode:%d", GetLastError());
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
			shutdown(_pSession->Socket, SD_BOTH);
		}
		// (1)
		else
		{
			// * GQCS Send/Recv 완료 통지 처리
			//  완료 통지 : TCP/IP suite가 관리하는 버퍼에 복사 성공
			if (pOverlapped == &pSession->RecvOverlapped)
				RecvComplete(dwTransferred);

			if (pOverlapped == &pSession->SendOverlapped)
				SendComplete(dwTransferred);
		}

		if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
			ReleaseSession();
	}
	LOG(L"SYSTEM", LOG_DEBUG, L"WorkerThread Exit");
	return 0;
}

mylib::CLanClient::stSESSION::stSESSION()
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
}

mylib::CLanClient::stSESSION::~stSESSION()
{
	_aligned_free(stIO);
}
