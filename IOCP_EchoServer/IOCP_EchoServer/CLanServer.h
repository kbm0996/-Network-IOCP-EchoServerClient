/*----------------------------------------------------------------------------
CLanServer

- bool Start(...) 오픈 IP / 포트 / 워커스레드 수 / Nagle 옵션 / 최대접속자 수
- void Stop(...)
- bool Disconnect(ClientID)  / UINT64
- int GetClientCount(...)
- SendPacket(ClientID, Packet *)   / UINT64
virtual void OnClientJoin(Client 정보 / ClientID / 기타등등) = 0;   < Accept 후 접속처리 완료 후 호출.
virtual void OnClientLeave(ClientID) = 0;   	           < Disconnect 후 호출
virtual bool OnConnectRequest(ClientIP,Port) = 0;          < accept 직후
	return false; 시 클라이언트 거부.
	return true; 시 접속 허용
virtual void OnRecv(ClientID, CNPacket *) = 0;             < 패킷 수신 완료 후
virtual void OnSend(ClientID, int sendsize) = 0;           < 패킷 송신 완료 후
virtual void OnWorkerThreadBegin() = 0;                    < 워커스레드 GQCS 바로 하단에서 호출
virtual void OnWorkerThreadEnd() = 0;                      < 워커스레드 1루프 종료 후
virtual void OnError(int errorcode, wchar *) = 0;

- 사용법
CServer LanServer;
void main()
{
	LanServer.Start(df_SERVER_IP, df_SERVER_PORT, 4, false, 500);
	while (!LanServer.IsShutdown())
	{
		LanServer.ServerControl();
		LanServer.PrintState();
	}
	LanServer.Stop();
}
----------------------------------------------------------------------------*/
#ifndef __CLAN_SERVER_H__
#define __CLAN_SERVER_H__
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

// sizeof(UINT64) == 8 Byte == 64 Bit
// [00000000 00000000 0000] [0000 00000000 00000000 00000000 00000000 00000000]
// 1. 상위 2.5 Byte = Index 영역
// 2. 하위 5.5 Byte = SessionID 영역
#define CreateSessionID(ID, Index)	(((UINT64)Index << 44) | ID)				
#define GetSessionIndex(SessionID)	((SessionID >> 44) & 0xfffff)
#define GetSessionID(SessionID)		(SessionID & 0x00000fffffffffff)

namespace mylib
{
	class CLanServer
	{
	public:
		CLanServer();
		virtual ~CLanServer();

		//////////////////////////////////////////////////////////////////////////
		// Server Control
		//
		//////////////////////////////////////////////////////////////////////////
		// Server ON/OFF
		bool Start(WCHAR *szIP, int iPort, int iWorkerThreadCnt, bool bNagle, __int64 iConnectMax);
		void Stop();

		// Monitoring (Bit operation)
		enum en_SERVER_MONITOR
		{
			en_CONNECT_CNT = 1,
			en_ACCEPT_CNT = 2,
			en_PACKET_TPS = 4,
			en_PACKETPOOL_SIZE = 8,
			en_ALL = 15
		};
		void PrintState(int iFlag = en_ALL);

	protected:
		// External Call
		bool SendPacket(UINT64 iSessionID, CNPacket *pPacket);
		bool SendPacket_Disconnect(UINT64 iSessionID, CNPacket *pPacket);
		bool DisconnectSession(UINT64 iSessionID);

		//////////////////////////////////////////////////////////////////////////
		// Notice
		//
		// OnConnectRequest		: Accept 직후, true/false 접속 허용/거부
		// OnClientJoin			: Accept 접속처리 완료 후, 유저 접속 관련
		// OnClientLeave		: Disconnect 후, 유저 정리
		// OnRecv				: 패킷 수신 후, 패킷 처리
		// OnSend				: 패킷 송신 후
		// OnError				: 에러 발생 후
		//////////////////////////////////////////////////////////////////////////
		virtual bool OnConnectRequest(WCHAR* wszIP, int iPort) = 0;
		virtual void OnClientJoin(UINT64 SessionID) = 0;
		virtual void OnClientLeave(UINT64 SessionID) = 0;
		virtual void OnRecv(UINT64 SessionID, CNPacket * pPacket) = 0;
		virtual void OnSend(UINT64 SessionID, int iSendSize) = 0;
		virtual void OnError(int iErrCode, WCHAR * wszErr) = 0;

	private:
		//////////////////////////////////////////////////////////////////////////
		// Session
		//
		//////////////////////////////////////////////////////////////////////////
		// * Session Sync Structure
		//  Send를 1회로 제한하는 이유
		// 1. 여러번 보내는 이점 X. 속도 느림
		// 2. 완료 통지 순서 보장
		// 3. Non-Paged Memory 발생 최소화
		struct stIOREF
		{
			stIOREF(LONG64 iCnt, LONG64 bRelease)
			{
				this->iCnt = iCnt;
				this->bRelease = bRelease;
			}
			LONG64 iCnt;
			LONG64 bRelease;
		};

		struct stSESSION
		{
			stSESSION();
			int			nIndex;		// 네트워크 처리용
			UINT64		iSessionID;	// 컨텐츠 처리용
			SOCKET		Socket;
			CRingBuffer	RecvQ;
			CLFQueue<CNPacket*> SendQ;
			OVERLAPPED	RecvOverlapped;
			OVERLAPPED	SendOverlapped;
			stIOREF*	stIO;

			LONG		bSendFlag;	// NP메모리 최소화, 동기화 문제 방지를 위해 Send를 1회씩 하기로 약속
			int			iSendPacketCnt;
			bool		bSendDisconnect;
		};

		//  외부에서 호출하는 함수(SendPacket, Disconnect)에서 필요
		stSESSION*	ReleaseSessionLock(UINT64 iSessionID);
		void		ReleaseSessionFree(stSESSION* pSession);
		bool		ReleaseSession(stSESSION* pSession);

		//////////////////////////////////////////////////////////////////////////
		// Network
		//
		//////////////////////////////////////////////////////////////////////////
		// IOCP Enrollment
		bool RecvPost(stSESSION* pSession);
		bool SendPost(stSESSION* pSession);
		// IOCP Completion Notice
		void RecvComplete(stSESSION* pSession, DWORD dwTransferred);
		void SendComplete(stSESSION* pSession, DWORD dwTransferred);
		// Network Thread
		static unsigned int CALLBACK MonitorThread(LPVOID pCLanServer); /// unused
		static unsigned int CALLBACK AcceptThread(LPVOID pCLanServer);
		static unsigned int CALLBACK WorkerThread(LPVOID pCLanServer);
		unsigned int MonitorThread_Process();
		unsigned int AcceptThread_Process();
		unsigned int WorkerThread_Process();

		//////////////////////////////////////////////////////////////////////////
		// Variable
		//
		//////////////////////////////////////////////////////////////////////////
		// Socket
		SOCKET				_ListenSocket;
		BOOL				_bServerOn;
		BOOL				_bNagle;
		// IOCP
		HANDLE				_hIOCP;
		// Threads
		HANDLE				_hMonitorThread;
		HANDLE				_hAcceptThread;
		HANDLE				_hWorkerThread[20];
		int					_iWorkerThreadMax;
		// Session
		stSESSION*			_SessionArr;
		UINT64				_iSessionID;
		CLFStack<UINT64>	_SessionStk;	// 빈 세션 저장용 Freelist

	protected:
		LONG64				_lConnectMax;
		LONG64				_lConnectCnt;
		// Encoding
		BYTE				_byCode;
		// Monitoring
		LONG64				_lAcceptCnt;
		LONG64				_lAcceptTps;
		LONG64				_lRecvTps;
		LONG64				_lSendTps;
	};
}

#define MON_CONNECT_CNT		mylib::CLanServer::en_CONNECT_CNT
#define MON_ACCEPT_CNT		mylib::CLanServer::en_ACCEPT_CNT
#define MON_PACKET_TPS		mylib::CLanServer::en_PACKET_TPS
#define MON_PACKETPOOL_SIZE	mylib::CLanServer::en_PACKETPOOL_SIZE
#define MON_ALL				mylib::CLanServer::en_ALL
#endif