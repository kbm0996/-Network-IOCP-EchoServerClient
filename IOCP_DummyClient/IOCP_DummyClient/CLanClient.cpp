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
	//  ������ �۽� ���� ũ�⸦ 0���� ����� ���� ���۸� �ǳʶٰ� �������� ������ �۽� ���ۿ� ���̷�Ʈ�� ���޵Ǿ� ���� ���.
	// ��, ���� ������ ũ��� �ǵ��� ����. �������� �Ұ� ���� ��Ʈ�� ���α׷� ������ ���������� �����ϴ°� �ƴ϶�
	// Ŭ�� �ִ´�� �޾ƾ� �ϹǷ� ��Ŷ ó�� �ӵ��� ���� �ӵ��� ������� ���� �� ����.
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
			PostQueuedCompletionStatus(_hIOCP, 0, 0, NULL); // ������ ���Ḧ ���� IOCP ��Ŷ�� �����Ͽ� IOCP�� ����
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
		wprintf(L" - RecvPacket TPS	: %lld \n", _lRecvTps);	// �ʴ� Recv ��Ŷ ���� - �Ϸ����� ������ Cnt, *���� �����尡 �ټ��̹Ƿ� interlocked �迭 �Լ� ���
		wprintf(L" - SendPacket TPS	: %lld \n", _lSendTps);	/// WSASend�� ȣ���� �� �ָ��� - ����, *���� �����尡 �ټ��̹Ƿ� interlocked �迭 �Լ� ���
	}
	if (bPacketPool)
		wprintf(L" - PacketPool Use	: %d \n", CNPacket::GetUseSize());	// ��� ���� �޸�Ǯ 	

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

	// TODO : shutdown() �Լ� ������ �� 
	//  shutdown() �Լ��� 4HandShake(1Fin - 2Ack+3Fin - 4Fin)���� ù��° Fin�� ������ ����. 
	// 1. ������� �̿� ���� Ack�� ������ ������(��� ������ '�������'���� ������� ���) ���ᰡ ���� ����
	//   - �� ����(���� ������ϴµ� �Ȳ������� ���)�� ���ӵǸ� �޸𸮰� ���������� �����ϰ� SandBuffer�� ���� �� ����
	// 2. TimeWait�� 100% ����(�ȳ������ 4HandShake�� ���ؾ���)
	//   - �̴� ũ�� �Ű� �� �ʿ�� ����...?
	//
	// TODO : CancelIoEx ���� �˾ƺ���
	//  shutdown���δ� ���� �� ���� ��� ��� (SendBuffer�� ���� á����, HeartBeat�� ����������)
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
	if (WSARecv(_pSession->Socket, wsabuf, iBufCnt, &dwTransferred, &dwFlag, &_pSession->RecvOverlapped, NULL) == SOCKET_ERROR) // ���� : 0, ���� : SOCKET_ERROR
	{
		int err = WSAGetLastError();
		if (err != ERROR_IO_PENDING) // ERROR_IO_PENDING : �񵿱� ������� �ϰ� �ִٴ� �ǹ�
		{
			if (err != 10038 && err != 10053 && err != 10054 && err != 10058)
			{
				// WSAENOTSOCK(10038)
				//	�ǹ� : ������ �ƴ� �׸� ���� ���� �۾��� �õ��߽��ϴ�.
				//	���� : ���� �ڵ� �Ű� ������ �ùٸ� ������ �������� �ʾҰų� fd_set�� ����� �ùٸ��� �ʽ��ϴ�.
				// WSAECONNABORTED(10053)
				//	�ǹ�: ����Ʈ����� ������ �ߴ��߽��ϴ�.
				//	���� : ȣ��Ʈ ��ǻ���� ����Ʈ��� ������ ������ �����߽��ϴ�. 
				//        ������ ���� �ð��� �ʰ��Ǿ��ų� �������� ������ �߻��߱� ������ �� �ֽ��ϴ�.
				// WSAECONNRESET(10054)
				//  �ǹ� : �Ǿ ������ �ٽ� �����߽��ϴ�.
				//	���� : ���� ȣ��Ʈ�� ���� ���� ������ ������ ���������ϴ�.
				//       ���� ȣ��Ʈ�� ���ڱ� �����ǰų� �ٽ� ���۵ǰų� �ϵ� ���Ḧ ����ϴ� ��쿡 �߻�
				// WSAESHUTDOWN(10058)
				//	�ǹ�: ������ ����� �Ŀ��� ������ �� �����ϴ�.
				//	���� : ������ shutdown���� �ش� ������ ������ �̹� ����Ǿ����Ƿ� ������ ���� �Ǵ� ���� ��û�� ������ �ʽ��ϴ�.
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
				//	�ǹ� : ������ �ƴ� �׸� ���� ���� �۾��� �õ��߽��ϴ�.
				//	���� : ���� �ڵ� �Ű� ������ �ùٸ� ������ �������� �ʾҰų� fd_set�� ����� �ùٸ��� �ʽ��ϴ�.
				// WSAECONNABORTED(10053)
				//	�ǹ�: ����Ʈ����� ������ �ߴ��߽��ϴ�.
				//	���� : ȣ��Ʈ ��ǻ���� ����Ʈ��� ������ ������ �����߽��ϴ�. 
				//        ������ ���� �ð��� �ʰ��Ǿ��ų� �������� ������ �߻��߱� ������ �� �ֽ��ϴ�.
				// WSAECONNRESET(10054)
				//  �ǹ� : �Ǿ ������ �ٽ� �����߽��ϴ�.
				//	���� : ���� ȣ��Ʈ�� ���� ���� ������ ������ ���������ϴ�.
				//       ���� ȣ��Ʈ�� ���ڱ� �����ǰų� �ٽ� ���۵ǰų� �ϵ� ���Ḧ ����ϴ� ��쿡 �߻�
				// WSAESHUTDOWN(10058)
				//	�ǹ�: ������ ����� �Ŀ��� ������ �� �����ϴ�.
				//	���� : ������ shutdown���� �ش� ������ ������ �̹� ����Ǿ����Ƿ� ������ ���� �Ǵ� ���� ��û�� ������ �ʽ��ϴ�.
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
	// ���� ��ŭ RecvQ.MoveWritePos �̵�
	_pSession->RecvQ.MoveWritePos(dwTransferred);

	//  * LanServer �� ��Ž� ���Ǵ� ���
	// Header�� Payload ���� ���� 2Byte ũ���� ��������
	WORD wHeader;
	while (1)
	{
		// RecvQ�� Header���̸�ŭ �ִ��� Ȯ��
		int iRecvSize = _pSession->RecvQ.GetUseSize();
		if (iRecvSize < sizeof(wHeader))
			break;

		// Packet ���� Ȯ�� (Header ���� + Payload ����)
		_pSession->RecvQ.Peek((char*)&wHeader, sizeof(wHeader));
		if (iRecvSize < sizeof(wHeader) + wHeader)
			break;

		// --HEADER 
		// ���� ��Ŷ���κ��� Header ����
		_pSession->RecvQ.MoveReadPos(sizeof(wHeader));

		// ���������� ����� Packet�� �Ҵ�
		CNPacket *pPacket = CNPacket::Alloc();

		// Packet�� �ִ� ũ�⺸�� Payload�� Ŭ ���
		if (pPacket->GetBufferSize() < wHeader)
		{
			pPacket->Free();
			shutdown(_pSession->Socket, SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"Payload Size Over");
			break;
		}

		// PacketPool�� Data �Ҵ�
		if (_pSession->RecvQ.Dequeue(pPacket->GetPayloadPtr(), wHeader) != wHeader)  // Header = Payload ����
		{
			pPacket->Free();
			shutdown(_pSession->Socket, SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"Payload Size specified in header and Actual Payload Size mismatch");
			break;
		}

		// --PAYLOAD
		// ���� ��Ŷ���κ��� Payload ����
		pPacket->MoveWritePos(wHeader);

		InterlockedIncrement64(&_lRecvTps);

		// ��Ŷ ó��
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
		// GQCS ���н� ���� ���� �״�� ��ȯ �� ���ڰ� �ʱ�ȭ �ʼ�
		dwTransferred = 0;
		pSession = NULL;
		pOverlapped = NULL;
		/*----------------------------------------------------------------------------------------------------------------
		// GQCS ���ϰ� //
		* �Ϸ� ��Ŷ�� ���������� dequeue�Ѵٸ� TRUE ����, ���� ���� O
		* lpOverlapped�� NULL�̰� �Ϸ� ��Ŷ�� dequeue�������ߴٸ� FALSE ����, ���� ���� X
		* lpOverlapped�� NOT NULL �̰� �Ϸ� ��Ŷ�� dequeue ������ ������ I/O �� ���� dequeue ��� FALSE�� ����, ���� ���� O
		* IOCP�� ��ϵ� ������ close �Ǹ�, ERROR_SUCCESS(0)�� ����, lpOverlapped�� non-NULL, lpNumberOfBytes�� 0�� ����
		----------------------------------------------------------------------------------------------------------------*/
		BOOL bResult = GetQueuedCompletionStatus(_hIOCP, &dwTransferred, (PULONG_PTR)&pSession, &pOverlapped, INFINITE);
		if (pOverlapped == NULL)
		{
			if (bResult == FALSE)
			{
				// GQCS ����
				int err = GetLastError();
				LOG(L"SYSTEM", LOG_ERROR, L"GetQueuedCompletionStatus() ErrorCode:%d", err);
			}
			PostQueuedCompletionStatus(_hIOCP, 0, NULL, NULL);
			//  Thread���� PostQueuedCompletionStatus()�� ����ϸ� Non-Paged Memory�� �þ����
			// Thread�� ȿ�������� ����ϴ� ������ �Ǳ⵵ ��
			break;
		}

		if (dwTransferred == 0)
		{
			// ���� ����
			shutdown(_pSession->Socket, SD_BOTH);
		}
		else
		{
			// * GQCS Send/Recv �Ϸ� ���� ó��
			//  �Ϸ� ������ TCP/IP suite�� �����ϴ� ���ۿ� ���縦 �ߴٴ� �ǹ�
			// �� ���Ĵ� TCP/IP���� �Ͼ�� ���̹Ƿ� �� �� ����
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
