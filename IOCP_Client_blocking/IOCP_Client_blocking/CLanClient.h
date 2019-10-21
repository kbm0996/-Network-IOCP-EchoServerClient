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
		// OnClientJoin			: ���� ���� ����
		// OnClientLeave		: ���� ���� ���� ����
		// OnRecv				: ��Ŷ ���� ��, ��Ŷ ó��
		// OnSend				: ��Ŷ �۽� ��
		// OnWorkerThreadBegin	: WorkerThread GQCS ����
		// OnWorkerThreadEnd	: WorkerThread Loop ���� ��
		// OnError				: ���� �߻� ��
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

		//  �ܺο��� ȣ���ϴ� �Լ�(SendPacket, Disconnect)���� �ʿ�
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