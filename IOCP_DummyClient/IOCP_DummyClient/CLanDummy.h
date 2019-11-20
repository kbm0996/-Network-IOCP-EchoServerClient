/*----------------------------------------------------------------------------
CLanClient

- bool Connect	���ε� IP, ����IP / ��Ŀ������ �� / Nagle �ɼ�
- bool Disconnect()
- bool SendPacket(Packet *)
virtual void OnClientJoin() = 0;		    < ���� ���� ��
virtual void OnClientLeave() = 0;			< ������ �������� ��
virtual void OnRecv(Packet *) = 0;			< �ϳ��� ��Ŷ ���� �Ϸ� ��
virtual void OnSend(int sendsize) = 0;		< ��Ŷ �۽� �Ϸ� ��
virtual void OnError(int errorcode, wchar *) = 0;

- ����
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
// 1. ���� 2.5 Byte = Index ����
// 2. ���� 5.5 Byte = SessionID ����
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
		// OnClientJoin			: ���� ���� ����
		// OnClientLeave		: ���� ���� ���� ����
		// OnRecv				: ��Ŷ ���� ��, ��Ŷ ó��
		// OnSend				: ��Ŷ �۽� ��
		// OnError				: ���� �߻� ��
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
		//  Send�� 1ȸ�� �����ϴ� ����
		// 1. ������ ������ ���� X. �ӵ� ����
		// 2. �Ϸ� ���� ���� ���� X
		// 3. Non-Paged Memory �߻�
		struct stIOREF
		{
			stIOREF(LONG64 iCnt, LONG64 bRelease)
			{
				this->iCnt = iCnt;
				this->bRelease = bRelease;
			}
			LONG64	iCnt;		// ����ȭ ����, �۾� ���۽� ++ �������� --
			LONG64	bRelease;	// Release ������ ����
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
			int			nIndex;		// ��Ʈ��ũ ó����
			UINT64		iSessionID;	// ������ ó����
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

		//  �ܺο��� ȣ���ϴ� �Լ�(SendPacket, Disconnect)���� �ʿ�
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