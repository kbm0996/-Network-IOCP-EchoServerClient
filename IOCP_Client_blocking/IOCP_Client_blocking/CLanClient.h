/*----------------------------------------------------------------------------
CLanClient

- bool Connect	바인딩 IP, 서버IP / 워커스레드 수 / Nagle 옵션
- bool Disconnect()
- bool SendPacket(Packet *)
virtual void OnClientJoin() = 0;		    < 연결 성공 후
virtual void OnClientLeave() = 0;			< 연결이 끊어졌을 때
virtual void OnRecv(Packet *) = 0;			< 하나의 패킷 수신 완료 후
virtual void OnSend(int sendsize) = 0;		< 패킷 송신 완료 후
virtual void OnError(int errorcode, wchar *) = 0;

- 사용법
CClient LanClient;
void main()
{
	LanClient.Connect(dfNETWORK_SERVER_IP, dfNETWORK_SERVER_PORT, 4, false);
	while (!LanClient.IsShutdown())
	{
		LanClient.ServerControl();
		LanClient.PrintState();
	}
	LanClient.Cleanp();
}
----------------------------------------------------------------------------*/
#ifndef __CLAN_CLIENT_H__
#define __CLAN_CLIENT_H__
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"winmm.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <mstcpip.h>
#include <io.h>
#include "CNPacket.h"
#include "CRingBuffer.h"
#include "CLFQueue.h"
#include "CLFStack.h"

namespace mylib
{
	class CLanClient
	{
	public:
		CLanClient();
		virtual ~CLanClient();

		//////////////////////////////////////////////////////////////////////////
		// Server Control
		//
		//////////////////////////////////////////////////////////////////////////
		// Client ON/OFF
		bool	Start(WCHAR* wszConnectIP, int iPort, int iWorkerThreadCnt, bool bNagle);
		void	Stop(bool bReconnect = false);

		// Monitoring (Bit operation)
		enum en_CLIENT_MONITOR
		{
			en_PACKET_TPS = 4,
			en_PACKETPOOL_SIZE = 8,
			en_ALL = 15
		};
		void	PrintState(int iFlag = en_ALL);

	protected:
		// External Call
		bool	SendPacket(CNPacket * pPacket);

		//////////////////////////////////////////////////////////////////////////
		// Notice
		//
		// OnClientJoin			: 서버 연결 직후
		// OnClientLeave		: 서버 연결 끊긴 이후
		// OnRecv				: 패킷 수신 후, 패킷 처리
		// OnSend				: 패킷 송신 후
		// OnWorkerThreadBegin	: WorkerThread GQCS 직후
		// OnWorkerThreadEnd	: WorkerThread Loop 종료 후
		// OnError				: 에러 발생 후
		//////////////////////////////////////////////////////////////////////////
		virtual void OnClientJoin() = 0;
		virtual void OnClientLeave() = 0;
		virtual void OnRecv(CNPacket * pPacket) = 0;
		virtual void OnSend(int iSendSize) = 0;
		virtual void OnError(int iErrCode, WCHAR * wszErr) = 0;

	private:
		//////////////////////////////////////////////////////////////////////////
		// Session
		//
		//////////////////////////////////////////////////////////////////////////
		// * Session Sync Structure
		//  Send를 1회로 제한하는 이유
		// 1. 여러번 보내는 이점 X. 속도 느림
		// 2. 완료 통지 순서 보장 X
		// 3. Non-Paged Memory 발생
		struct stIOREF
		{
			stIOREF(LONG64 iCnt, LONG64 bRelease)
			{
				this->iCnt = iCnt;
				this->bRelease = bRelease;
			}
			LONG64	iCnt;		// 동기화 수단, 작업 시작시 ++ 마쳤을시 --
			LONG64	bRelease;	// Release 중인지 여부
		};
		struct stSESSION
		{
			stSESSION();
			virtual ~stSESSION();
			UINT64		iSessionID;
			SOCKET		Socket;
			CRingBuffer	RecvQ;
			CLFQueue<CNPacket*> SendQ;
			OVERLAPPED	RecvOverlapped;
			OVERLAPPED	SendOverlapped;
			stIOREF*	stIO;

			LONG		bSendFlag;
			int			iSendPacketCnt;
		};

		//  외부에서 호출하는 함수(SendPacket, Disconnect)에서 필요
		bool	ReleaseSession();

		//////////////////////////////////////////////////////////////////////////
		// Network
		//
		//////////////////////////////////////////////////////////////////////////
		// IOCP Enrollment
		bool	RecvPost();
		bool	SendPost();
		// IOCP Completion Notice
		void	RecvComplete(DWORD dwTransferred);
		void	SendComplete(DWORD dwTransferred);
		// Network Thread
		static unsigned int CALLBACK MonitorThread(LPVOID pCLanClient); /// unused
		static unsigned int CALLBACK WorkerThread(LPVOID pCLanClient);
		unsigned int MonitorThread_Process();
		unsigned int WorkerThread_Process();

		//////////////////////////////////////////////////////////////////////////
		// Variable
		//
		//////////////////////////////////////////////////////////////////////////
		// Session
		stSESSION*	_pSession;
		WCHAR		_szServerIP[16];
		int			_iPort;
		BOOL		_bConnect;
		// IOCP
		HANDLE		_hIOCP;
		// Threads
		int			_iWorkerThreadMax;
		HANDLE		_hWorkerThread[20];
	protected:
		// Monitoring
		LONG64		_lConnectTryTps;
		LONG64		_lConnectSuccessTps;
		LONG64		_lConnectCnt;
		LONG64		_lConnectFailCnt;
		LONG64		_lRecvTps;
		LONG64		_lSendTps;
	};
}

#define MON_CLIENT_PACKET_TPS		mylib::CLanClient::en_PACKET_TPS
#define MON_CLIENT_PACKETPOOL_SIZE	mylib::CLanClient::en_PACKETPOOL_SIZE
#define MON_CLIENT_ALL				mylib::CLanClient::en_ALL
#endif