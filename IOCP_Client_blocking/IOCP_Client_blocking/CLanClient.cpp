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
	//  ������ �۽� ���� ũ�⸦ 0���� ����� ���� ���۸� �ǳʶٰ� �������� ������ �۽� ���ۿ� ���̷�Ʈ�� ���޵Ǿ� ���� ���.
	// ��, ���� ������ ũ��� �ǵ��� ����. �������� �Ұ� ���� ��Ʈ�� ���α׷� ������ ���������� �����ϴ°� �ƴ϶�
	// Ŭ�� �ִ´�� �޾ƾ� �ϹǷ� ��Ŷ ó�� �ӵ��� ���� �ӵ��� ������� ���� �� ����.
	///int iBufSize = 0;
	///int iOptLen = sizeof(iBufSize);
	///setsockopt(_pSession->Socket, SOL_SOCKET, SO_SNDBUF, (char *)&iBufSize, iOptLen);

	// Thread start
	for (int i = 0; i < iWorkerThreadCnt; ++i)
		_hWorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, NULL);

	if (CreateIoCompletionPort((HANDLE)_pSession->Socket, _hIOCP, (ULONG_PTR)_pSession, 0) == NULL)
	{
		//  HANDLE ���ڿ� ������ �ƴ� ���� �� ��� �߸��� �ڵ�(6�� ����) �߻�. 
		// ������ �ƴ� ���� �־��ٴ� ���� �ٸ� �����忡�� ������ ��ȯ�ߴٴ� �ǹ��̹Ƿ�  ����ȭ ������ ���ɼ��� ����.
		int err = WSAGetLastError();
		LOG(L"SYSTEM", LOG_ERROR, L"Session IOCP Enrollment ErrorCode:%d", err);
		if (InterlockedDecrement64(&_pSession->stIO->iCnt) == 0)
			ReleaseSession();
		return false;
	}

	InterlockedIncrement64(&_pSession->stIO->iCnt);

	OnClientJoin();

	// SessionSocket�� recv ���·� ����
	RecvPost();

	// Accept�ϸ鼭 Accept ��Ŷ�� �����鼭 IOCount�� 1�� �����ϹǷ� �ٽ� 1 ����
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
			PostQueuedCompletionStatus(_hIOCP, 0, 0, NULL); // ������ ���Ḧ ���� IOCP ��Ŷ�� �����Ͽ� IOCP�� ����
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
			wprintf(L" - RecvPacket TPS	: %lld \n", _lRecvTps);	// �ʴ� Recv ��Ŷ ���� - �Ϸ����� ������ Cnt, *���� �����尡 �ټ��̹Ƿ� interlocked �迭 �Լ� ���
			wprintf(L" - SendPacket TPS	: %lld \n", _lSendTps);	/// WSASend�� ȣ���� �� �ָ��� - ����, *���� �����尡 �ټ��̹Ƿ� interlocked �迭 �Լ� ���
			_lRecvTps = 0;
			_lSendTps = 0;
		}
		if (iFlag & en_PACKETPOOL_SIZE)
			wprintf(L" - PacketPool Use	: %d \n", CNPacket::GetUseSize());	// ��� ���� �޸�Ǯ 	
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
	//  shutdown() �Լ��� 4HandShake(1Fin - 2Ack+3Fin - 4Fin)���� 1Fin�� ������ �Լ�
	// 1. ������� �̿� ���� Ack�� ������ ������(��� ������ '�������'���� ������� ���) ���ᰡ ���� ����
	//   - �� ����(���� ������ϴµ� �Ȳ������� ���)�� ���ӵǸ� �޸𸮰� ���������� �����ϰ� SendBuffer�� ���� �� ����
	// 2. TimeWait�� ����
	//   - CancelIoEx ����Ͽ� 4HandShake�� �����ϰ� �ݾƾ� ���� �ʴ´�
	shutdown(_pSession->Socket, SD_BOTH);

	// TODO : CancelIoEx()
	//  shutdown���δ� ���� �� ���� ��� ��� (SendBuffer�� ���� á����, Heartbeat�� ����������)
	// CancelIoEx()�� WSARecv() ���� ������ �߻��Ѵ�. ������ �߻��ϴ��� shutdown()�� WSARecv()�� �����ϵ��� �����Ѵ�.
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
	if (WSARecv(_pSession->Socket, wsabuf, iBufCnt, &dwTransferred, &dwFlag, &_pSession->RecvOverlapped, NULL) == SOCKET_ERROR) // ���� : 0, ���� : SOCKET_ERROR
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
		// WSA_IO_PENDING(997) : �񵿱� ����� ��. Overlapped ������ ���߿� �Ϸ�� ���̴�. ��ø ������ ���� �غ� �Ǿ�����, ��� �Ϸ���� �ʾ��� ��� �߻�
		if (err != ERROR_IO_PENDING)
		{
			// WSAENOTSOCK(10038) : ������ �ƴ� �׸� ���� �۾� �õ�
			// WSAECONNABORTED(10053) : ȣ��Ʈ�� ���� ����. ������ ���� �ð� �ʰ�, �������� ���� �߻�
			// WSAECONNRESET(10054) : ���� ȣ��Ʈ�� ���� ���� ���� ���� ����. ���� ȣ��Ʈ�� ���ڱ� �����ǰų� �ٽ� ���۵ǰų� �ϵ� ���Ḧ ����ϴ� ���
			// WSAESHUTDOWN(10058) : ���� ���� �� ����
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
	// ���ŷ���ŭ RecvQ.MoveWritePos �̵�
	_pSession->RecvQ.MoveWritePos(dwTransferred);

	// Header�� Payload ���̸� ���� 2Byte ũ���� Ÿ��
	WORD wHeader;
	while (1)
	{
		// 1. RecvQ�� 'sizeof(Header)'��ŭ �ִ��� Ȯ��
		int iRecvSize = _pSession->RecvQ.GetUseSize();
		if (iRecvSize < sizeof(wHeader))
		{
			// �� �̻� ó���� ��Ŷ�� ����
			// ����, Header ũ�⺸�� ���� ������ �����Ͱ� �����ȴٸ� 2������ �ɷ���
			break;
		}

		// 2. Packet ���� Ȯ�� : Headerũ��('sizeof(Header)') + Payload����('Header')
		_pSession->RecvQ.Peek(reinterpret_cast<char*>(&wHeader), sizeof(wHeader));
		if (iRecvSize < sizeof(wHeader) + wHeader)
		{
			// Header�� Payload�� ���̰� ������ �ٸ�
			shutdown(_pSession->Socket, SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"Header & PayloadLength mismatch");
			break;
		}
		// RecvQ���� Packet�� Header �κ� ����
		_pSession->RecvQ.MoveReadPos(sizeof(wHeader));

		CNPacket *pPacket = CNPacket::Alloc();

		// 3. Payload ���� Ȯ�� : PacketBuffer�� �ִ� ũ�⺸�� Payload�� Ŭ ���
		if (pPacket->GetBufferSize() < wHeader)
		{
			pPacket->Free();
			shutdown(_pSession->Socket, SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"PacketBufferSize < PayloadSize ");
			break;
		}

		// 4. PacketPool�� Packet ������ �Ҵ�
		if (_pSession->RecvQ.Dequeue(pPacket->GetPayloadPtr(), wHeader) != wHeader) 
		{
			pPacket->Free();
			LOG(L"SYSTEM", LOG_WARNG, L"RecvQ dequeue error");
			shutdown(_pSession->Socket, SD_BOTH);
			break;
		}

		// RecvQ���� Packet�� Payload �κ� ����
		pPacket->MoveWritePos(wHeader);

		++_lRecvTps;

		// 5. Packet ó��
		OnRecv(pPacket);
		pPacket->Free();
	}

	// SessionSocket�� recv ���·� ����
	RecvPost();
}

void mylib::CLanClient::SendComplete(DWORD dwTransferred)
{
	OnSend(dwTransferred);

	// ���� ��Ŷ �� ��ŭ �����
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
				LOG(L"SYSTEM", LOG_ERROR, L"GetQueuedCompletionStatus() ErrorCode:%d", GetLastError());
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
			shutdown(_pSession->Socket, SD_BOTH);
		}
		// (1)
		else
		{
			// * GQCS Send/Recv �Ϸ� ���� ó��
			//  �Ϸ� ���� : TCP/IP suite�� �����ϴ� ���ۿ� ���� ����
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
