#include "CLanClient.h"

mylib::CLanClient::CLanClient()
{
	_pSession = new stSESSION();
	_lRecvTps = 0;
	_lSendTps = 0;
}

mylib::CLanClient::~CLanClient()
{
}

bool mylib::CLanClient::Start(WCHAR * wszIP, int iPort, int iWorkerThreadCnt, bool bNagle)
{
	if (_pSession->Socket != INVALID_SOCKET)
		return false;

	int err;
	_iPort = iPort;
	_iWorkerThreadMax = iWorkerThreadCnt;
	memcpy(_szServerIP, wszIP, sizeof(_szServerIP));

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		err = WSAGetLastError();
		LOG(L"SYSTEM", LOG_ERROR, L"WSAStartup() ErrorCode: %d", err);
		return false;
	}

	_pSession->Socket = socket(AF_INET, SOCK_STREAM, 0);
	if (_pSession->Socket == INVALID_SOCKET)
	{
		err = WSAGetLastError();
		LOG(L"SYSTEM", LOG_ERROR, L"socket() ErrorCode: %d", err);
		return false;
	}

	setsockopt(_pSession->Socket, IPPROTO_TCP, TCP_NODELAY, (char *)&bNagle, sizeof(bNagle));

	SOCKADDR_IN serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(iPort);
	InetPton(AF_INET, wszIP, &serveraddr.sin_addr);
	if (connect(_pSession->Socket, (SOCKADDR *)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR)
	{
		err = WSAGetLastError();
		LOG(L"SYSTEM", LOG_ERROR, L"connect() ErrorCode: %d", err);
		return false;
	}

	// SOL_SOCKET - SO_SNDBUF
	//  소켓의 송신 버퍼 크기를 0으로 만들면 소켓 버퍼를 건너뛰고 프로토콜 스택의 송신 버퍼에 다이렉트로 전달되어 성능 향상.
	// 단, 수신 버퍼의 크기는 건들지 않음. 도착지를 잃고 수신 파트는 프로그램 구조상 서버측에서 제어하는게 아니라
	// 클라가 주는대로 받아야 하므로 패킷 처리 속도가 수신 속도를 따라오지 못할 수 있음.
	///int iBufSize = 0;
	///setsockopt(_pSession->Socket, SOL_SOCKET, SO_SNDBUF, (char *)&iBufSize, sizeof(iBufSize));

	_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	if (_hIOCP == NULL)
	{
		err = WSAGetLastError();
		LOG(L"SYSTEM", LOG_ERROR, L"CreateIoCompletionPort() ErrorCode: %d", err);
		return false;
	}

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
	RecvPost();

	if (InterlockedDecrement64(&_pSession->stIO->iCnt) == 0)
		ReleaseSession();


	LOG(L"SYSTEM", LOG_SYSTM, L"Client Start");
	return true;
}

void mylib::CLanClient::Stop(bool bReconnect)
{
	closesocket(_pSession->Socket);
	ReleaseSession();

	if (!bReconnect)
	{
		// WorkerThread Exit
		for (int i = 0; i < _iWorkerThreadMax; ++i)
			PostQueuedCompletionStatus(_hIOCP, 0, 0, NULL); // 스레드 종료를 위한 IOCP 패킷을 생성하여 IOCP에 전송
		WaitForMultipleObjects(_iWorkerThreadMax, _hWorkerThread, TRUE, INFINITE);
		for (int i = 0; i < _iWorkerThreadMax; ++i)
			CloseHandle(_hWorkerThread[i]);

		CloseHandle(_hIOCP);

		WSACleanup();
	}

	// Socket
	_pSession->Socket = INVALID_SOCKET;

	// Monitoring
	_lRecvTps = 0;
	_lSendTps = 0;

	///LOG(L"SYSTEM", LOG_DEBUG, L"Client Stop");
}

void mylib::CLanClient::PrintState(int iFlag)
{
	if (bPacketTPS || bPacketPool)
	{
		wprintf(L"===========================================\n");
		wprintf(L" Lan Client\n");
		wprintf(L"===========================================\n");
	}
	if (bPacketTPS)
	{
		wprintf(L" - RecvPacket TPS	: %lld \n", _lRecvTps);	// 초당 Recv 패킷 개수 - 완료통지 왔을때 Cnt, *접근 스레드가 다수이므로 interlocked 계열 함수 사용
		wprintf(L" - SendPacket TPS	: %lld \n", _lSendTps);	/// WSASend를 호출할 때 애매함 - 보류, *접근 스레드가 다수이므로 interlocked 계열 함수 사용
	}
	if (bPacketPool)
		wprintf(L" - PacketPool Use	: %d \n", CNPacket::GetUseSize());	// 사용 중인 메모리풀 	

	_lRecvTps = 0;
	_lSendTps = 0;
}

bool mylib::CLanClient::SendPacket(CNPacket * pPacket)
{
	pPacket->AddRef();
	_pSession->SendQ.Enqueue(pPacket);

	SendPost();

	return true;
}

bool mylib::CLanClient::ReleaseSession()
{
	shutdown(_pSession->Socket, SD_BOTH);
	closesocket(_pSession->Socket);

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

	// TODO : shutdown() 함수 주의할 점 
	//  shutdown() 함수는 4HandShake(1Fin - 2Ack+3Fin - 4Fin)에서 첫번째 Fin을 보내는 행위. 
	// 1. 상대편에서 이에 대한 Ack를 보내지 않으면(대게 상대방이 '응답없음'으로 먹통됐을 경우) 종료가 되지 않음
	//   - 이 현상(연결 끊어야하는데 안끊어지는 경우)이 지속되면 메모리가 순간적으로 증가하고 SandBuffer가 터질 수 있음
	// 2. TimeWait이 100% 남음(안남기려면 4HandShake를 안해야함)
	//   - 이는 크게 신경 쓸 필요는 없음...?
	//
	// TODO : CancelIoEx 사용법 알아보기
	//  shutdown으로는 닫을 수 없을 경우 사용 (SendBuffer가 가득 찼을때, HeartBeat가 끊어졌을때)
}

bool mylib::CLanClient::RecvPost()
{
	WSABUF wsabuf[2];
	DWORD dwTransferred = 0;
	DWORD dwFlag = 0;
	int iBufCnt = 1;

	wsabuf[0].buf = _pSession->RecvQ.GetWriteBufferPtr();
	wsabuf[0].len = _pSession->RecvQ.GetUnbrokenEnqueueSize();
	if (_pSession->RecvQ.GetUnbrokenEnqueueSize() < _pSession->RecvQ.GetFreeSize())
	{
		wsabuf[1].buf = _pSession->RecvQ.GetBufferPtr();
		wsabuf[1].len = _pSession->RecvQ.GetFreeSize() - wsabuf[0].len;
		++iBufCnt;
	}
	ZeroMemory(&_pSession->RecvOverlapped, sizeof(_pSession->RecvOverlapped));
	InterlockedIncrement64(&_pSession->stIO->iCnt);
	if (WSARecv(_pSession->Socket, wsabuf, iBufCnt, &dwTransferred, &dwFlag, &_pSession->RecvOverlapped, NULL) == SOCKET_ERROR) // 성공 : 0, 실패 : SOCKET_ERROR
	{
		int err = WSAGetLastError();
		if (err != ERROR_IO_PENDING) // ERROR_IO_PENDING : 비동기 입출력을 하고 있다는 의미
		{
			if (err != 10038 && err != 10053 && err != 10054 && err != 10058)
			{
				// WSAENOTSOCK(10038)
				//	의미 : 소켓이 아닌 항목에 대해 소켓 작업을 시도했습니다.
				//	설명 : 소켓 핸들 매개 변수가 올바른 소켓을 참조하지 않았거나 fd_set의 멤버가 올바르지 않습니다.
				// WSAECONNABORTED(10053)
				//	의미: 소프트웨어에서 연결을 중단했습니다.
				//	설명 : 호스트 컴퓨터의 소프트웨어가 설정된 연결을 중지했습니다. 
				//        데이터 전송 시간이 초과되었거나 프로토콜 오류가 발생했기 때문일 수 있습니다.
				// WSAECONNRESET(10054)
				//  의미 : 피어가 연결을 다시 설정했습니다.
				//	설명 : 원격 호스트에 의해 기존 연결이 강제로 끊어졌습니다.
				//       원격 호스트가 갑자기 중지되거나 다시 시작되거나 하드 종료를 사용하는 경우에 발생
				// WSAESHUTDOWN(10058)
				//	의미: 소켓이 종료된 후에는 전송할 수 없습니다.
				//	설명 : 이전의 shutdown에서 해당 방향의 소켓이 이미 종료되었으므로 데이터 전송 또는 수신 요청이 허용되지 않습니다.
				LOG(L"SYSTEM", LOG_ERROR, L"WSARecv() ErrorCode:%d / Socket:%d / RecvQ Size:%d", err, _pSession->Socket, _pSession->RecvQ.GetUseSize());
			}
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
	DWORD dwTransferred;
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
	ZeroMemory(&_pSession->SendOverlapped, sizeof(_pSession->SendOverlapped));
	InterlockedIncrement64(&_pSession->stIO->iCnt);
	if (WSASend(_pSession->Socket, wsabuf, iBufCnt, &dwTransferred, 0, &_pSession->SendOverlapped, NULL) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		if (err != ERROR_IO_PENDING)
		{
			if (err != 10038 && err != 10053 && err != 10054 && err != 10058)
			{
				// WSAENOTSOCK(10038)
				//	의미 : 소켓이 아닌 항목에 대해 소켓 작업을 시도했습니다.
				//	설명 : 소켓 핸들 매개 변수가 올바른 소켓을 참조하지 않았거나 fd_set의 멤버가 올바르지 않습니다.
				// WSAECONNABORTED(10053)
				//	의미: 소프트웨어에서 연결을 중단했습니다.
				//	설명 : 호스트 컴퓨터의 소프트웨어가 설정된 연결을 중지했습니다. 
				//        데이터 전송 시간이 초과되었거나 프로토콜 오류가 발생했기 때문일 수 있습니다.
				// WSAECONNRESET(10054)
				//  의미 : 피어가 연결을 다시 설정했습니다.
				//	설명 : 원격 호스트에 의해 기존 연결이 강제로 끊어졌습니다.
				//       원격 호스트가 갑자기 중지되거나 다시 시작되거나 하드 종료를 사용하는 경우에 발생
				// WSAESHUTDOWN(10058)
				//	의미: 소켓이 종료된 후에는 전송할 수 없습니다.
				//	설명 : 이전의 shutdown에서 해당 방향의 소켓이 이미 종료되었으므로 데이터 전송 또는 수신 요청이 허용되지 않습니다.
				LOG(L"SYSTEM", LOG_ERROR, L"WSASend() # ErrorCode:%d / Socket:%d / IOCnt:%d / SendQ Size:%d", err, _pSession->Socket, _pSession->stIO->iCnt, _pSession->SendQ.GetUseSize());
			}
			InterlockedExchange(&_pSession->bSendFlag, FALSE);
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
	// 받은 만큼 RecvQ.MoveWritePos 이동
	_pSession->RecvQ.MoveWritePos(dwTransferred);

	//  * LanServer 간 통신시 사용되는 헤더
	// Header는 Payload 길이 담은 2Byte 크기의 데이터형
	WORD wHeader;
	while (1)
	{
		// RecvQ에 Header길이만큼 있는지 확인
		int iRecvSize = _pSession->RecvQ.GetUseSize();
		if (iRecvSize < sizeof(wHeader))
			break;

		// Packet 길이 확인 (Header 길이 + Payload 길이)
		_pSession->RecvQ.Peek((char*)&wHeader, sizeof(wHeader));
		if (iRecvSize < sizeof(wHeader) + wHeader)
			break;

		// --HEADER 
		// 받은 패킷으로부터 Header 제거
		_pSession->RecvQ.MoveReadPos(sizeof(wHeader));

		// 내부적으로 사용할 Packet을 할당
		CNPacket *pPacket = CNPacket::Alloc();

		// Packet의 최대 크기보다 Payload가 클 경우
		if (pPacket->GetBufferSize() < wHeader)
		{
			pPacket->Free();
			shutdown(_pSession->Socket, SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"Payload Size Over");
			break;
		}

		// PacketPool에 Data 할당
		if (_pSession->RecvQ.Dequeue(pPacket->GetPayloadPtr(), wHeader) != wHeader)  // Header = Payload 길이
		{
			pPacket->Free();
			shutdown(_pSession->Socket, SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"Payload Size specified in header and Actual Payload Size mismatch");
			break;
		}

		// --PAYLOAD
		// 받은 패킷으로부터 Payload 제거
		pPacket->MoveWritePos(wHeader);

		InterlockedIncrement64(&_lRecvTps);

		// 패킷 처리
		OnRecv(pPacket);
		pPacket->Free();
	}

	RecvPost();
}

void mylib::CLanClient::SendComplete(DWORD dwTransferred)
{
	OnSend(dwTransferred);

	CNPacket *pPacket;
	for (int i = 0; i < _pSession->iSendPacketCnt; ++i)
	{
		pPacket = nullptr;
		if (_pSession->SendQ.Dequeue(pPacket))
		{
			pPacket->Free();
			InterlockedIncrement64(&_lSendTps);
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
	DWORD		dwTransferred;
	stSESSION*	pSession;
	LPOVERLAPPED pOverlapped;
	while (1)
	{
		// GQCS 실패시 이전 값을 그대로 반환 → 인자값 초기화 필수
		dwTransferred = 0;
		pSession = NULL;
		pOverlapped = NULL;
		/*----------------------------------------------------------------------------------------------------------------
		// GQCS 리턴값 //
		* 완료 패킷을 성공적으로 dequeue한다면 TRUE 리턴, 인자 세팅 O
		* lpOverlapped가 NULL이고 완료 패킷을 dequeue하지못했다면 FALSE 리턴, 인자 세팅 X
		* lpOverlapped가 NOT NULL 이고 완료 패킷을 dequeue 했지만 실패한 I/O 에 대한 dequeue 라면 FALSE를 리턴, 인자 세팅 O
		* IOCP에 등록된 소켓이 close 되면, ERROR_SUCCESS(0)을 리턴, lpOverlapped에 non-NULL, lpNumberOfBytes에 0을 세팅
		----------------------------------------------------------------------------------------------------------------*/
		BOOL bResult = GetQueuedCompletionStatus(_hIOCP, &dwTransferred, (PULONG_PTR)&pSession, &pOverlapped, INFINITE);
		if (pOverlapped == NULL)
		{
			if (bResult == FALSE)
			{
				// GQCS 에러
				int err = GetLastError();
				LOG(L"SYSTEM", LOG_ERROR, L"GetQueuedCompletionStatus() ErrorCode:%d", err);
			}
			PostQueuedCompletionStatus(_hIOCP, 0, NULL, NULL);
			//  Thread에서 PostQueuedCompletionStatus()를 사용하면 Non-Paged Memory가 늘어나지만
			// Thread를 효율적으로 사용하는 수단이 되기도 함
			break;
		}

		if (dwTransferred == 0)
		{
			// 세션 종료
			shutdown(_pSession->Socket, SD_BOTH);
		}
		else
		{
			// * GQCS Send/Recv 완료 통지 처리
			//  완료 통지란 TCP/IP suite가 관리하는 버퍼에 복사를 했다는 의미
			// 그 이후는 TCP/IP에서 일어나는 일이므로 알 수 없음
			if (pOverlapped == &pSession->RecvOverlapped)
			{
				RecvComplete(dwTransferred);
			}

			if (pOverlapped == &pSession->SendOverlapped)
			{
				SendComplete(dwTransferred);
			}
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
