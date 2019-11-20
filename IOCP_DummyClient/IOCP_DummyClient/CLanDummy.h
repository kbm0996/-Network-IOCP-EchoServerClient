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
#include <vector>
// sizeof(UINT64) == 8 Byte == 64 Bit
// [00000000 00000000 0000] [0000 00000000 00000000 00000000 00000000 00000000]
// 1. 상위 2.5 Byte = Index 영역
// 2. 하위 5.5 Byte = SessionID 영역
#define CreateSessionID(SessionID, Index)	(((UINT64)Index << 44) | SessionID)				
#define GetSessionIndex(ui64SessionID)	((ui64SessionID >> 44) & 0xfffff)
#define GetSessionID(ui64SessionID)		(ui64SessionID & 0x00000fffffffffff)

namespace mylib
{
	class CLanDummy
	{
	public:
		CLanDummy();
		virtual ~CLanDummy();

		//////////////////////////////////////////////////////////////////////////
		// Server Control
		//
		//////////////////////////////////////////////////////////////////////////
		// Client ON/OFF
		bool Start(WCHAR* wszConnectIP, int iPort, bool bNagle, __int64 iConnectMax, int iWorkerThreadCnt, bool bDisconnect, int iOversendCnt, int iDisconnectDelay, int iLoopDelay);
		void Stop();

		
	protected:
		struct stTHREAD_INFO;
		struct stSESSION;
		bool ConnectSession(int nIndex);
		void DisconnectSession(SOCKET socket);

		// External Call
		bool SendPacket(UINT64 iSessionID, CNPacket * pPacket);
		bool SendPacket_Disconnect(UINT64 iSessionID, CNPacket * pPacket);

		//////////////////////////////////////////////////////////////////////////
		// Notice
		//
		// OnClientJoin			: 서버 연결 직후
		// OnClientLeave		: 서버 연결 끊긴 이후
		// OnRecv				: 패킷 수신 후, 패킷 처리
		// OnSend				: 패킷 송신 후
		// OnError				: 에러 발생 후
		//////////////////////////////////////////////////////////////////////////
		virtual void OnClientJoin(UINT64 SessionID) = 0;
		virtual void OnClientLeave(UINT64 SessionID) = 0;
		virtual void OnRecv(UINT64 SessionID, CNPacket * pPacket) = 0;
		virtual void OnSend(UINT64 SessionID, int iSendSize) = 0;
		virtual void OnError(int iErrCode, WCHAR * wszErr) = 0;
		virtual void OnEcho(stSESSION * pSession) = 0;

	///private:
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
			enum enSTAT
			{
				en_CONNECT,
				en_SEND,
				en_DISCONNECT,
				en_RELEASE
			};
			stSESSION();
			virtual ~stSESSION();
			int			nIndex;		// 네트워크 처리용
			UINT64		iSessionID;	// 컨텐츠 처리용
			SOCKET		Socket;
			CRingBuffer	RecvQ;
			CLFQueue<CNPacket*> SendQ;
			OVERLAPPED	RecvOverlapped;
			OVERLAPPED	SendOverlapped;
			stIOREF*	stIO;

			LONG		bSendFlag;
			int			iSendPacketCnt;
			bool		bSendDisconnect;

			// Echo Test
			stTHREAD_INFO * pThreadInfo;
			CLFQueue<ULONGLONG> qEcho;
			ULONGLONG	lLastEcho;

			ULONGLONG	lLastRecvTick; 
			ULONGLONG	lLastLoginTick;
			enSTAT		lStatus;
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
		void RecvComplete(stSESSION * pSession, DWORD dwTransferred);
		void SendComplete(stSESSION * pSession, DWORD dwTransferred);
		// Network Thread
		static unsigned int CALLBACK MonitorThread(LPVOID pCLanDummy); /// unused
		static unsigned int CALLBACK UpdateThread(LPVOID pCLanDummy);
		static unsigned int CALLBACK WorkerThread(LPVOID pCLanDummy);
		unsigned int MonitorThread_Process();
		unsigned int UpdateThread_Process(stTHREAD_INFO * pInfo);
		unsigned int WorkerThread_Process();

		//////////////////////////////////////////////////////////////////////////
		// Variable
		//
		//////////////////////////////////////////////////////////////////////////
	protected:
		// Server
		WCHAR				_szServerIP[16];
		int					_iPort;
		SOCKADDR_IN			_serveraddr;
		BOOL				_bServerOn;
		BOOL				_bNagle;
		HANDLE				_hIOCP;

		// Threads
		struct stTHREAD_INFO
		{
			CLanDummy * pThisClass;
			SRWLOCK srwSessionLock;
			std::vector<stSESSION*> vtSession;
		};
		stTHREAD_INFO *		_arrThreadInfo;
		int					_iWorkerThreadMax;
		int					_iUpdateThreadMax;
		int					_iLoopDelay;
		HANDLE*				_hWorkerThread;
		HANDLE*				_hUpdateThread;
		LONG64 _nData;

		// Session
		stSESSION*			_arrSession;
		UINT64				_iSessionID;
		LONG64				_lConnectMax;

	public:
		// Test Control
		bool				_bDisconnect;
		bool				_bSendEcho;
		int					_iDisconnectDelay;
		int					_iOversendCnt;

		// Monitor
		LONG64				_lThreadLoopTps;
		LONG64				_lConnectTryTps;
		LONG64				_lConnectSuccessTps;
		LONG64				_lConnectCnt;
		LONG64				_lConnectFailCnt;

		LONG64				_lRecvTps;
		LONG64				_lSendTps;
	};
}

#endif