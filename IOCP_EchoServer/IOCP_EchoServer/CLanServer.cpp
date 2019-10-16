#include "CLanServer.h"

mylib::CLanServer::CLanServer()
{
	
	_bServerOn = 0;
	_iSessionID = 0;
	_ListenSocket = INVALID_SOCKET;
	_lConnectCnt = 0;
	_lAcceptCnt = 0;
	_lAcceptTps = 0;
	_lRecvTps = 0;
	_lSendTps = 0;
}

mylib::CLanServer::~CLanServer()
{
}

bool mylib::CLanServer::Start(WCHAR * szIP, int iPort, int iWorkerThreadCnt, bool bNagle, __int64 iConnectMax)
{
	if (_ListenSocket != INVALID_SOCKET)
		return false;

	_bNagle = bNagle;
	_iWorkerThreadMax = iWorkerThreadCnt;
	_lConnectMax = iConnectMax;

	// Session Init
	_SessionArr = new stSESSION[iConnectMax];
	for (__int64 i = iConnectMax - 1; i >= 0; --i)
	{
		_SessionArr[i].nIndex = i;
		_SessionStk.Push(i);
	}
	
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

	_ListenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (_ListenSocket == INVALID_SOCKET)
	{
		LOG(L"SYSTEM", LOG_ERROR, L"socket() failed %d", WSAGetLastError());
		return false;
	}

	// Server on
	sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(iPort);
	InetPton(AF_INET, szIP, reinterpret_cast<PVOID>(&serveraddr.sin_addr));
	if (bind(_ListenSocket, reinterpret_cast<sockaddr*>(&serveraddr), sizeof(serveraddr)) == SOCKET_ERROR)
	{
		LOG(L"SYSTEM", LOG_ERROR, L"bind() failed %d", WSAGetLastError());
		return false;
	}
	if (listen(_ListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		LOG(L"SYSTEM", LOG_ERROR, L"listen() failed %d", WSAGetLastError());
		return false;
	}

	// TODO: SOL_SOCKET - SO_SNDBUF
	//  ������ �۽� ���� ũ�⸦ 0���� ����� ���� ���۸� �ǳʶٰ� �������� ������ �۽� ���ۿ� ���̷�Ʈ�� ���޵Ǿ� ���� ���.
	// ��, ���� ������ ũ��� �ǵ��� ����. �������� �Ұ� ���� ��Ʈ�� ���α׷� ������ ���������� �����ϴ°� �ƴ϶�
	// Ŭ�� �ִ´�� �޾ƾ� �ϹǷ� ��Ŷ ó�� �ӵ��� ���� �ӵ��� ������� ���� �� ����.
	//int iBufSize = 0;
	//int iOptLen = sizeof(iBufSize);
	//setsockopt(_ListenSocket, SOL_SOCKET, SO_SNDBUF, (char *)&iBufSize, iOptLen);

	// Thread start
	_hAcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, 0, NULL);
	for (int i = 0; i < iWorkerThreadCnt; ++i)
		_hWorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, NULL);

	_bServerOn = TRUE;

	LOG(L"SYSTEM", LOG_SYSTM, L"Start");
	return true;
}

void mylib::CLanServer::Stop()
{
	_bServerOn = FALSE;

	// Server off
	closesocket(_ListenSocket);

	// Thread end
	WaitForSingleObject(_hAcceptThread, INFINITE);
	CloseHandle(_hAcceptThread);

	
	for (int i = 0; i < _iWorkerThreadMax; ++i)	// WorkerThread ���� �޼����� IOCP Queue�� ���� ����
		PostQueuedCompletionStatus(_hIOCP, 0, 0, 0);	
	WaitForMultipleObjects(_iWorkerThreadMax, _hWorkerThread, TRUE, INFINITE);
	for (int i = 0; i < _iWorkerThreadMax; ++i)
		CloseHandle(_hWorkerThread[i]);

	// Winsock close
	WSACleanup();

	// IOCP close
	CloseHandle(_hIOCP);

	// Session Release
	for (int i = 0; i < _lConnectMax; ++i)
	{
		if (_SessionArr[i].Socket != INVALID_SOCKET)
			ReleaseSession(&_SessionArr[i]);
		_aligned_free(_SessionArr[i].stIO);
	}
	if (_SessionArr != NULL)
	{
		delete[] _SessionArr;
		_SessionArr = NULL;
	}

	_iSessionID = 0;
	_ListenSocket = INVALID_SOCKET;
	_iWorkerThreadMax = 0;
	_lConnectCnt = 0;
	_lAcceptCnt = 0;
	_lAcceptTps = 0;
	_lRecvTps = 0;
	_lSendTps = 0;
	_SessionStk.Clear();

	LOG(L"SYSTEM", LOG_DEBUG, L"Server Stop");
}

void mylib::CLanServer::PrintState(int iFlag)
{
	if (iFlag & en_ALL)
	{
		wprintf(L"===========================================\n");
		wprintf(L" Lan Server\n");
		wprintf(L"===========================================\n");
		if (iFlag & en_CONNECT_CNT)
		{
			//  TODO : �뷮���� ������ ������ ��ȣ�� lost�� �Ǿ� ConnectSession�� ���� ��찡 ����
			// netstat���� ��Ʈ Ȯ���ϰ� ESTABLISH ���� Ȯ�� : TCP �������� �������� �´���, �� �ڵ���� �������� �ľ�
			// ESTABLISH�� ConnectSession�� �ٸ���, ���� �󿡼� Heartbeat, TCP ���������� KeepAlive. ������, �ڵ���� ����
			wprintf(L" - ConnectSession	: %lld \n", _lConnectCnt);	// ���ӵ� ������ ���ΰ�	 ++, -- // LanServer
			wprintf(L"\n");
		}
		if (iFlag & en_ACCEPT_CNT)
		{
			wprintf(L" - Accept TPS		: %lld \n", _lAcceptTps);		// ���� ī����
			wprintf(L" - Accept Total		: %lld \n", _lAcceptCnt);	// ������ ī���� 
			wprintf(L"\n");
			_lAcceptTps = 0;
		}
		if (iFlag & en_PACKET_TPS)
		{
			wprintf(L" - RecvPacket TPS	: %lld \n", _lRecvTps);	// �ʴ� Recv ��Ŷ ���� - �Ϸ����� ������ Cnt, *���� �����尡 �ټ��̹Ƿ� interlocked �迭 �Լ� ���
			wprintf(L" - SendPacket TPS	: %lld \n", _lSendTps);	/// WSASend�� ȣ���� �� �ָ��� - ����, *���� �����尡 �ټ��̹Ƿ� interlocked �迭 �Լ� ���
			_lRecvTps = 0;
			_lSendTps = 0;
		}
		if (iFlag & en_PACKETPOOL_SIZE)
			wprintf(L" - PacketPool Use	: %d \n", CNPacket::GetUseSize());
	}
	
}

bool mylib::CLanServer::SendPacket(UINT64 iSessionID, CNPacket * pPacket)
{
	stSESSION *pSession = ReleaseSessionLock(iSessionID);
	if (pSession == nullptr)
		return false;

	pPacket->AddRef();
	pSession->SendQ.Enqueue(pPacket);
	SendPost(pSession);

	ReleaseSessionFree(pSession);
	return true;
}

bool mylib::CLanServer::SendPacket_Disconnect(UINT64 iSessionID, CNPacket * pPacket)
{
	stSESSION *pSession = ReleaseSessionLock(iSessionID);
	if (pSession == nullptr)
		return false;

	pPacket->AddRef();
	pSession->SendQ.Enqueue(pPacket);
	SendPost(pSession);

	pSession->bSendDisconnect = true;

	ReleaseSessionFree(pSession);
	return true;
}

bool mylib::CLanServer::DisconnectSession(UINT64 iSessionID)
{
	stSESSION *pSession = ReleaseSessionLock(iSessionID);
	if (pSession == nullptr)
		return false;

	shutdown(pSession->Socket, SD_BOTH);

	ReleaseSessionFree(pSession);
	return true;
}

mylib::CLanServer::stSESSION * mylib::CLanServer::ReleaseSessionLock(UINT64 iSessionID)
{
	//  TODO : Multi-Thread ȯ�濡�� Release, Connect, Send, Accept ���� ContextSwitching���� ���� ���� �߻��� �� ������ �����ؾ� ��
	int nIndex = GetSessionIndex(iSessionID);
	stSESSION *pSession = &_SessionArr[nIndex];

	/************************************************************/
	/* ���� ���� ���¿��� �ٸ� �����尡 Release�� �õ����� ��� */
	/************************************************************/
	// **CASE 0 : Release �÷��װ� TRUE�� Ȯ���� �ٲ� ����
	if (pSession->stIO->bRelease == TRUE)
		return nullptr;

	// **CASE 1 : ReleaseSession ���� ���� ���
	// �̶� �ٸ� �����忡�� SendPacket�̳� Disconnect �õ� ��, IOCnt�� 1�� �� �� ����
	if (InterlockedIncrement64(&pSession->stIO->iCnt) == 1)
	{
		// �����ϸ鼭 ������Ų IOCnt�� �ٽ� ���ҽ��� ����
		if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
			ReleaseSession(pSession); // Release�� ��ø���� �ϴ� ������ Release ������ ó��
		return nullptr;
	}

	// **CASE 2 : �̹� Disconnect �� ���� Accept�Ͽ� ���� Session�� ������ ���
	if (pSession->iSessionID != iSessionID) // �� SessionID�� ContextSwitching �߻� ������ SessionID�� ��
	{
		if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
			ReleaseSession(pSession);
		return nullptr;
	}

	// **CASE 3 : Release �÷��װ� FALSE���� Ȯ���� ����
	if (pSession->stIO->bRelease == FALSE)
		return pSession;

	// **CASE 4 : CASE 3�� �����ϱ� ������ TRUE�� �ٲ���� ���
	if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
		ReleaseSession(pSession);
	return nullptr;
}

void mylib::CLanServer::ReleaseSessionFree(stSESSION * pSession)
{
	if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
		ReleaseSession(pSession);
	return;
}

bool mylib::CLanServer::ReleaseSession(stSESSION * pSession)
{
	stIOREF stComparentIO(0, FALSE);
	if (!InterlockedCompareExchange128((LONG64*)pSession->stIO, TRUE, 0, (LONG64*)&stComparentIO))
		return false;

	// TODO : shutdown()
	//  shutdown() �Լ��� 4HandShake(1Fin - 2Ack+3Fin - 4Fin)���� 1Fin�� ������ �Լ�
	// 1. ������� �̿� ���� Ack�� ������ ������(��� ������ '�������'���� ������� ���) ���ᰡ ���� ����
	//   - �� ����(���� ������ϴµ� �Ȳ������� ���)�� ���ӵǸ� �޸𸮰� ���������� �����ϰ� SendBuffer�� ���� �� ����
	// 2. TimeWait�� ����
	//   - CancelIoEx ����Ͽ� 4HandShake�� �����ϰ� �ݾƾ� ���� �ʴ´�
	shutdown(pSession->Socket, SD_BOTH);

	// TODO : CancelIoEx()
	//  shutdown���δ� ���� �� ���� ��� ��� (SendBuffer�� ���� á����, Heartbeat�� ����������)
	// CancelIoEx()�� WSARecv() ���� ������ �߻��Ѵ�. ������ �߻��ϴ��� shutdown()�� WSARecv()�� �����ϵ��� �����Ѵ�.
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
	_SessionStk.Push(pSession->nIndex);
	InterlockedDecrement64(&_lConnectCnt);
	return true;
}

bool mylib::CLanServer::RecvPost(stSESSION * pSession)
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
	if (WSARecv(pSession->Socket, wsabuf, iBufCnt, &dwTransferred, &dwFlag, &pSession->RecvOverlapped, NULL) == SOCKET_ERROR) // ���� : 0, ���� : SOCKET_ERROR
	{
		int err = WSAGetLastError();
		// WSA_IO_PENDING(997) : �񵿱� ����� ��. Overlapped ������ ���߿� �Ϸ�� ���̴�. ��ø ������ ���� �غ� �Ǿ�����, ��� �Ϸ���� �ʾ��� ��� �߻�
		if (err != WSA_IO_PENDING) 
		{
			// WSAENOTSOCK(10038) : ������ �ƴ� �׸� ���� �۾� �õ�
			// WSAECONNABORTED(10053) : ȣ��Ʈ�� ���� ����. ������ ���� �ð� �ʰ�, �������� ���� �߻�
			// WSAECONNRESET(10054) : ���� ȣ��Ʈ�� ���� ���� ���� ���� ����. ���� ȣ��Ʈ�� ���ڱ� �����ǰų� �ٽ� ���۵ǰų� �ϵ� ���Ḧ ����ϴ� ���
			// WSAESHUTDOWN(10058) : ���� ���� �� ����
			if (err != 10038 && err != 10053 && err != 10054 && err != 10058)
				LOG(L"SYSTEM", LOG_ERROR, L"WSARecv() failed%d / Socket:%d / RecvQ Size:%d", err, pSession->Socket, pSession->RecvQ.GetUseSize());

			shutdown(pSession->Socket,SD_BOTH);
			if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
				ReleaseSession(pSession);
			return false;
		}
	}
	return true;
}

bool mylib::CLanServer::SendPost(stSESSION * pSession)
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
	ZeroMemory(&pSession->SendOverlapped, sizeof(OVERLAPPED));
	InterlockedIncrement64(&pSession->stIO->iCnt);
	if (WSASend(pSession->Socket, wsabuf, iBufCnt, &dwTransferred, 0, &pSession->SendOverlapped, NULL) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		// WSA_IO_PENDING(997) : �񵿱� ����� ��. Overlapped ������ ���߿� �Ϸ�� ���̴�. ��ø ������ ���� �غ� �Ǿ�����, ��� �Ϸ���� �ʾ��� ��� �߻�
		if (err != ERROR_IO_PENDING)
		{
			// WSAENOTSOCK(10038) : ������ �ƴ� �׸� ���� �۾� �õ�
			// WSAECONNABORTED(10053) : ȣ��Ʈ�� ���� ����. ������ ���� �ð� �ʰ�, �������� ���� �߻�
			// WSAECONNRESET(10054) : ���� ȣ��Ʈ�� ���� ���� ���� ���� ����. ���� ȣ��Ʈ�� ���ڱ� �����ǰų� �ٽ� ���۵ǰų� �ϵ� ���Ḧ ����ϴ� ���
			// WSAESHUTDOWN(10058) : ���� ���� �� ����
			if (err != 10038 && err != 10053 && err != 10054 && err != 10058)
				LOG(L"SYSTEM", LOG_ERROR, L"WSASend() # failed%d / Socket:%d / IOCnt:%d / SendQ Size:%d", err, pSession->Socket, pSession->stIO->iCnt, pSession->SendQ.GetUseSize());

			//InterlockedExchange(&pSession->bSendFlag, FALSE);
			shutdown(pSession->Socket, SD_BOTH);
			if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
				ReleaseSession(pSession);

			return false;
		}
	}
	return true;
}

void mylib::CLanServer::RecvComplete(stSESSION * pSession, DWORD dwTransferred)
{
	// ���ŷ���ŭ RecvQ.MoveWritePos �̵�
	pSession->RecvQ.MoveWritePos(dwTransferred);

	// Header�� Payload ���̸� ���� 2Byte ũ���� Ÿ��
	WORD wHeader;
	while (1)
	{
		// 1. RecvQ�� 'sizeof(Header)'��ŭ �ִ��� Ȯ��
		int iRecvSize = pSession->RecvQ.GetUseSize();
		if (iRecvSize < sizeof(wHeader))
		{
			// �� �̻� ó���� ��Ŷ�� ����
			// ����, Header ũ�⺸�� ���� ������ �����Ͱ� �����ȴٸ� 2������ �ɷ���
			break;
		}

		// 2. Packet ���� Ȯ�� : Headerũ��('sizeof(Header)') + Payload����('Header')
		pSession->RecvQ.Peek(reinterpret_cast<char*>(&wHeader), sizeof(wHeader));
		if (iRecvSize < sizeof(wHeader) + wHeader)
		{
			// Header�� Payload�� ���̰� ������ �ٸ�
			shutdown(pSession->Socket, SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"Header & PayloadLength mismatch");
			break;
		}
		// RecvQ���� Packet�� Header �κ� ����
		pSession->RecvQ.MoveReadPos(sizeof(wHeader));

		CNPacket * pPacket = CNPacket::Alloc();

		// 3. Payload ���� Ȯ�� : PacketBuffer�� �ִ� ũ�⺸�� Payload�� Ŭ ���
		if (pPacket->GetBufferSize() < wHeader)
		{
			pPacket->Free();
			shutdown(pSession->Socket,SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"PacketBufferSize < PayloadSize ");
			break;
		}

		// 4. PacketPool�� Packet ������ �Ҵ�
		if (pSession->RecvQ.Dequeue(pPacket->GetPayloadPtr(), wHeader) != wHeader)
		{
			pPacket->Free();
			LOG(L"SYSTEM", LOG_WARNG, L"RecvQ dequeue error");
			shutdown(pSession->Socket,SD_BOTH);
			break;
		}

		// RecvQ���� Packet�� Payload �κ� ����
		pPacket->MoveWritePos(wHeader);

		++_lRecvTps;
		
		// 5. Packet ó��
		OnRecv(pSession->iSessionID, pPacket);
		pPacket->Free();
	}

	// SessionSocket�� recv ���·� ����
	RecvPost(pSession);
}

void mylib::CLanServer::SendComplete(stSESSION * pSession, DWORD dwTransferred)
{
	OnSend(pSession->iSessionID, dwTransferred);

	// ���� ��Ŷ �� ��ŭ �����
	CNPacket * pPacket;
	for (int i = 0; i < pSession->iSendPacketCnt; ++i)
	{
		if (pSession->SendQ.Dequeue(pPacket))
		{
			pPacket->Free();
			++_lSendTps;
		}
	}

	if (pSession->bSendDisconnect == TRUE)
		shutdown(pSession->Socket, SD_BOTH);

	InterlockedExchange(&pSession->bSendFlag, FALSE);
	SendPost(pSession);
}

unsigned int mylib::CLanServer::MonitorThread(LPVOID pCLanServer)
{
	return ((CLanServer *)pCLanServer)->MonitorThread_Process();
}

unsigned int mylib::CLanServer::AcceptThread(LPVOID pCLanServer)
{
	return ((CLanServer *)pCLanServer)->AcceptThread_Process();
}

unsigned int mylib::CLanServer::WorkerThread(LPVOID pCLanServer)
{
	return ((CLanServer *)pCLanServer)->WorkerThread_Process();
}

unsigned int mylib::CLanServer::MonitorThread_Process()
{
	LOG(L"SYSTEM", LOG_DEBUG, L"MonitorThread Exit");
	return 0;
}

unsigned int mylib::CLanServer::AcceptThread_Process()
{
	while (1)
	{
		sockaddr_in SessionAddr;
		int iAddrlen = sizeof(SessionAddr);
		SOCKET SessionSocket = WSAAccept(_ListenSocket, reinterpret_cast<sockaddr*>(&SessionAddr), &iAddrlen, NULL, NULL);
		if (!_bServerOn)
			break;
		if (SessionSocket == INVALID_SOCKET)
		{
			int err = WSAGetLastError();
			LOG(L"SYSTEM", LOG_ERROR, L"WSAAccept() failed%d", err);
			break;
		}

		++_lAcceptTps;
		++_lAcceptCnt;

		if (_lConnectCnt >= _lConnectMax)
		{
			LOG(L"SYSTEM", LOG_SYSTM, L"Connect Full %d/%d", _lConnectCnt, _lConnectMax);
			closesocket(SessionSocket);
			continue;
		}

		WCHAR wszIP[INET_ADDRSTRLEN];
		InetNtop(AF_INET, &SessionAddr.sin_addr, wszIP, 16);
		int iPort = ntohs(SessionAddr.sin_port);
		if (!OnConnectRequest(wszIP, iPort))
		{
			LOG(L"SYSTEM", LOG_SYSTM, L"Blocked IP:%s:%d", wszIP, iPort);
			closesocket(SessionSocket);
			continue;
		}

		// TODO : TCP ���Ͽ��� ���񼼼� ó��
		//  ���� ������ ������������ ����(��� ���� ����, �ܺο��� ������ �Ϻη� ���� ���)�Ǿ� 
		// ������ Ŭ���̾�Ʈ���� ������ ����Ǿ����� �𸣴� ������ ������ �ǹ�(Close �̺�Ʈ�� ������)
		// TCP Keepalive ��� 
		// - SO_KEEPALIVE : �ý��� ������Ʈ�� �� ����. �ý����� ��� SOCKET�� ���ؼ� KEEPALIVE ����
		// - SIO_KEEPALIVE_VALS : Ư�� SOCKET�� KEEPALIVE ����
		tcp_keepalive tcpkl;
		tcpkl.onoff = TRUE;
		tcpkl.keepalivetime = 30000; // ms
		tcpkl.keepaliveinterval = 1000; 
		WSAIoctl(SessionSocket, SIO_KEEPALIVE_VALS, &tcpkl, sizeof(tcp_keepalive), 0, 0, NULL, NULL, NULL);

		setsockopt(SessionSocket, IPPROTO_TCP, TCP_NODELAY, (char *)&_bNagle, sizeof(_bNagle));

		// Session Alloc
		UINT64 nIndex = -1;
		_SessionStk.Pop(nIndex);
		stSESSION *pSession = &_SessionArr[nIndex];
		pSession->Socket = SessionSocket;
		pSession->iSessionID = CreateSessionID(++_iSessionID, nIndex);
		InterlockedIncrement64(&pSession->stIO->iCnt);
		pSession->RecvQ.Clear();
		pSession->SendQ.Clear();
		pSession->bSendFlag = 0;
		pSession->iSendPacketCnt = 0;
		pSession->bSendDisconnect = FALSE;
		if (CreateIoCompletionPort((HANDLE)SessionSocket, _hIOCP, (ULONG_PTR)pSession, 0) == NULL)
		{
			//  HANDLE ���ڿ� ������ �ƴ� ���� �� ��� �߸��� �ڵ�(6�� ����) �߻�
			// ������ �ƴ� ���� �־��ٴ� ���� �ٸ� �����忡�� ������ ��ȯ�ߴٴ� �ǹ��̹Ƿ� ����ȭ ������ ���ɼ��� ����.
			LOG(L"SYSTEM", LOG_ERROR, L"AcceptThread() - CreateIoCompletionPort() failed%d", WSAGetLastError());
			if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
				ReleaseSession(pSession);
			continue;
		}
		pSession->stIO->bRelease = FALSE;

		OnClientJoin(pSession->iSessionID);

		// SessionSocket�� recv ���·� ����
		RecvPost(pSession);

		// Accept�ϸ鼭 Accept ��Ŷ�� �����鼭 IOCount�� 1�� �����ϹǷ� �ٽ� 1 ����
		if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
			ReleaseSession(pSession);

		InterlockedIncrement64(&_lConnectCnt);
	}
	LOG(L"SYSTEM", LOG_DEBUG, L"AcceptThread Exit");
	return 0;
}

unsigned int mylib::CLanServer::WorkerThread_Process()
{
	DWORD		dwTransferred;
	stSESSION *	pSession;
	LPOVERLAPPED pOverlapped;
	while (1)
	{
		// GQCS ���н� ���� ���� �״�� ��ȯ �� ���ڰ� �ʱ�ȭ �ʼ�
		dwTransferred = 0;
		pSession = 0;
		pOverlapped = 0;
		/*----------------------------------------------------------------------------------------------------------------
		// GQCS ���ϰ� //
		
		(1) IOCP Queue�κ��� �Ϸ� ��Ŷ dequeue ������ ���.
		 -> TRUE ����, lpCompletionKey ����

		(2) IOCP Queue�κ��� �Ϸ� ��Ŷ dequeue ���� 
			&& lpOverlapped�� NULL(0)�� ���. 
		 -> FALSE ����
	
		(3) IOCP Queue�κ��� �Ϸ� ��Ŷ dequeue ���� 
			&& lpOverlapped�� NOT_NULL
			&& dequeue�� ���(�޼���)�� ������ I/O�� ���.
		 -> FALSE ����, lpCompletionKey ����

		(4) IOCP�� ��ϵ� Socket�� close���� ���.
		 -> FALSE ����, lpOverlapped�� NOT_NULL, lpNumberOfBytes�� 0 ����
		----------------------------------------------------------------------------------------------------------------*/
		BOOL bResult = GetQueuedCompletionStatus(_hIOCP, &dwTransferred, reinterpret_cast<PULONG_PTR>(&pSession), &pOverlapped, INFINITE);
		if (pOverlapped == 0)
		{
			// (2)
			if (bResult == FALSE)	
			{
				LOG(L"SYSTEM", LOG_ERROR, L"GetQueuedCompletionStatus() failed %d", GetLastError());
				PostQueuedCompletionStatus(_hIOCP, 0, 0, 0);	// GQCS ���� �� ��ġ�� ���Ҹ��Ѱ� �����Ƿ� ����
				break;
			}

			// (x) ���� ���� (PQCS�� ������ ���� �޼���)
			if (dwTransferred == 0 && pSession == 0)	
			{
				PostQueuedCompletionStatus(_hIOCP, 0, 0, 0);
				break;
			}
		}
		
		// (3)(4) ���� ����
		if (dwTransferred == 0)
		{
			shutdown(pSession->Socket, SD_BOTH);
		}
		// (1)
		else	
		{
			// * GQCS Send/Recv �Ϸ� ���� ó��
			//  �Ϸ� ���� : TCP/IP suite�� �����ϴ� ���ۿ� ���� ����
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

mylib::CLanServer::stSESSION::stSESSION()
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
