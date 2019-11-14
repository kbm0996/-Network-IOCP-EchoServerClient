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

	// Session Init
	_SessionArr = new stSESSION[iConnectMax];
	for (int i = 0; i < iConnectMax; ++i)
	{
		_SessionArr[i].nIndex = i;
		_SessionArr[i].iSessionID = CreateSessionID(++_iSessionID, i);
		///_SessionStk.Push(i);
	}

	// Thread start
	_hWorkerThread = new HANDLE[_iWorkerThreadMax];
	for (int i = 0; i < _iWorkerThreadMax; ++i)
		_hWorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, NULL);

	_bServerOn = TRUE;

	_arrThreadInfo = new stTHREAD_INFO[_iUpdateThreadMax];
	for (int i = 0; i < _iUpdateThreadMax; ++i)
		_arrThreadInfo[i].pThisClass = this;

	for (int i = 0; i < _lConnectMax; ++i)
	{
		stSESSION * pSession = &_SessionArr[i];
		_arrThreadInfo[i%_iUpdateThreadMax].Sessionlist.push_front(pSession);
	}

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
		PostQueuedCompletionStatus(_hIOCP, 0, 0, NULL); // ������ ���Ḧ ���� IOCP ��Ŷ�� �����Ͽ� IOCP�� ����

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
		if (_SessionArr[i].Socket != INVALID_SOCKET)
			ReleaseSession(&_SessionArr[i]);
	}

	if (_SessionArr != nullptr)
		delete[] _SessionArr;
	
	if (_hWorkerThread != nullptr)
		delete[] _hWorkerThread;
	
	_lRecvTps = 0;
	_lSendTps = 0;

	LOG(L"SYSTEM", LOG_DEBUG, L"Client Stop");
}

bool mylib::CLanDummy::ConnectSession(int nIndex)
{
	stSESSION * pSession = &_SessionArr[nIndex];
	pSession->Socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (pSession->Socket == INVALID_SOCKET)
	{
		LOG(L"SYSTEM", LOG_ERROR, L"socket() failed %d", WSAGetLastError());
		return false;
	}


	setsockopt(pSession->Socket, IPPROTO_TCP, TCP_NODELAY, (char *)&_bNagle, sizeof(_bNagle));

	// Server connect
	SOCKADDR_IN serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(_iPort);
	InetPton(AF_INET, _szServerIP, reinterpret_cast<PVOID>(&serveraddr.sin_addr));
	if (connect(pSession->Socket, reinterpret_cast<sockaddr*>(&serveraddr), sizeof(serveraddr)) == SOCKET_ERROR)
	{
		LOG(L"SYSTEM", LOG_ERROR, L"connect() failed %d", WSAGetLastError());
		closesocket(pSession->Socket);
		return false;
	}

	InterlockedIncrement64(&pSession->stIO->iCnt);

	pSession->RecvQ.Clear();
	pSession->SendQ.Clear();
	pSession->bSendFlag = FALSE;
	pSession->iSendPacketCnt = 0;
	pSession->bSendDisconnect = FALSE;

	pSession->lLastLoginTick = pSession->lLastRecvTick = GetTickCount64();
	pSession->stkEcho.Clear();

	if (CreateIoCompletionPort((HANDLE)pSession->Socket, _hIOCP, (ULONG_PTR)pSession, 0) == NULL)
	{
		//  HANDLE ���ڿ� ������ �ƴ� ���� �� ��� �߸��� �ڵ�(6�� ����) �߻�. 
		// ������ �ƴ� ���� �־��ٴ� ���� �ٸ� �����忡�� ������ ��ȯ�ߴٴ� �ǹ��̹Ƿ�  ����ȭ ������ ���ɼ��� ����.
		LOG(L"SYSTEM", LOG_ERROR, L"Session IOCP Enrollment failed %d", WSAGetLastError());
		if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
			ReleaseSession(pSession);
		return false;
	}

	pSession->stIO->bRelease = FALSE;

	OnClientJoin(pSession->iSessionID);

	// SessionSocket�� recv ���·� ����
	RecvPost(pSession);

	// Accept�ϸ鼭 Accept ��Ŷ�� �����鼭 IOCount�� 1�� �����ϹǷ� �ٽ� 1 ����
	if (InterlockedDecrement64(&pSession->stIO->iCnt) == 0)
	{
		ReleaseSession(pSession);
		return false;
	}

	InterlockedExchange(&pSession->lStatus, stSESSION::enSTAT::en_SEND);
	return true;
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
	closesocket(pSession->Socket);

	OnClientLeave(pSession->iSessionID);

	CNPacket *pPacket = nullptr;
	while (pSession->SendQ.Dequeue(pPacket))
	{
		if (pPacket != nullptr)
			pPacket->Free();
		pPacket = nullptr;
	}

	InterlockedExchange(&pSession->bSendFlag, FALSE);
	pSession->Socket = INVALID_SOCKET;
	pSession->stkEcho.Clear();
	InterlockedDecrement64(&_lConnectCnt);

	InterlockedExchange(&pSession->lStatus, stSESSION::enSTAT::en_CONNECT);

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

			shutdown(pSession->Socket, SD_BOTH);
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

void mylib::CLanDummy::RecvComplete(stSESSION * pSession, DWORD dwTransferred)
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

		CNPacket *pPacket = CNPacket::Alloc();

		// 3. Payload ���� Ȯ�� : PacketBuffer�� �ִ� ũ�⺸�� Payload�� Ŭ ���
		if (pPacket->GetBufferSize() < wHeader)
		{
			pPacket->Free();
			shutdown(pSession->Socket, SD_BOTH);
			LOG(L"SYSTEM", LOG_WARNG, L"PacketBufferSize < PayloadSize ");
			break;
		}

		// 4. PacketPool�� Packet ������ �Ҵ�
		if (pSession->RecvQ.Dequeue(pPacket->GetPayloadPtr(), wHeader) != wHeader)
		{
			pPacket->Free();
			LOG(L"SYSTEM", LOG_WARNG, L"RecvQ dequeue error");
			shutdown(pSession->Socket, SD_BOTH);
			break;
		}

		// RecvQ���� Packet�� Payload �κ� ����
		pPacket->MoveWritePos(wHeader);

		InterlockedIncrement64(&_lRecvTps);

		// 5. Packet ó��
		OnRecv(pSession->iSessionID, pPacket);
		pPacket->Free();
	}

	// SessionSocket�� recv ���·� ����
	RecvPost(pSession);


}

void mylib::CLanDummy::SendComplete(stSESSION * pSession, DWORD dwTransferred)
{
	OnSend(pSession->iSessionID, dwTransferred);

	// ���� ��Ŷ �� ��ŭ �����
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
		shutdown(pSession->Socket, SD_BOTH);

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
	std::list<stSESSION*> * Sessionlist = &((stTHREAD_INFO *)pThreadInfo)->Sessionlist;
	return pCLanDummy->UpdateThread_Process(Sessionlist);
}


unsigned int mylib::CLanDummy::WorkerThread(LPVOID pCLanDummy)
{
	return ((CLanDummy *)pCLanDummy)->WorkerThread_Process();
}

unsigned int mylib::CLanDummy::MonitorThread_Process()
{
	return 0;
}

unsigned int mylib::CLanDummy::UpdateThread_Process(std::list<stSESSION*> * Sessionlist)
{
	while (_bServerOn)
	{
		for (auto iter = Sessionlist->begin(); iter != Sessionlist->end(); ++iter)
		{
			stSESSION * pSession = *iter;
			if (pSession->lStatus == stSESSION::enSTAT::en_CONNECT)
			{
				InterlockedIncrement64(&_lConnectTryTps);
				if (!ConnectSession(pSession->nIndex))
				{
					InterlockedIncrement64(&_lConnectFailCnt);
				}
				else
				{
					InterlockedIncrement64(&_lConnectSuccessTps);
					InterlockedIncrement64(&_lConnectCnt);
				}
			}

			if (pSession->stkEcho.GetUseSize() == 0)
			{
				if (pSession->lStatus == stSESSION::enSTAT::en_SEND)
				{
					if (_bSendEcho)
						OnEcho(pSession);
					if (_bDisconnect)
						InterlockedExchange(&pSession->lStatus, stSESSION::enSTAT::en_DISCONNECT);
				}
				else if (pSession->lStatus == stSESSION::enSTAT::en_DISCONNECT
					&& GetTickCount64() - pSession->lLastLoginTick >= _iDisconnectDelay
					&& _bDisconnect)
				{
					shutdown(pSession->Socket, SD_BOTH);
					///InterlockedExchange(&pSession->lStatus, stSESSION::enSTAT::en_RELEASE);
				}
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
