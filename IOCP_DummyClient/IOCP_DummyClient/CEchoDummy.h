#ifndef __NETWORK_H__
#define __NETWORK_H__

#include "_Define.h"
#include "CLanDummy.h"
#include <list>
#include <map>

/*
CLanServer 중간 테스트.

LanServer EchoDummy Port : 6000

# 사용법

ServerIP :  알아서 입력
Disconnec Test :  Yes 선택시 무작위로 재접속을 시도 함.
Client Count :  테스트 클라이언트 수 입니다.  1 명 / 50 명 / 100 명  선택,
OverSendCount :  Echo 응답이 오지 않아도 보내는 수치 입니다.
기본은 1로 테스트 해주시고 문제가 없다면 늘립니다.

Loop Delay ms :  더미 클라이언트의 컨텐츠 루프 딜레이 값 입니다.
적으면 적을 수록 에코를 빨리빨리 보냅니다.


# 실행화면에서

Error - Connect Fail  :  클라에서 connect 함수가 실패한 경우 입니다.  서버가 accept 를 안하거나 빠르게 accept 를 안했을 경우 나타날 수 있음.

* Error - Disconnect from Server : 서버가 일방적으로 끊은겁니다.  지금은 서버가 끊을 상황이 없으므로 동기화 오류로 봐야 합니다.
이 수치가 발생하는 경우는 서버의 AcceptTotal 과 더미의 ConnectTotal 수치를 비교 합니다.
Total 값이 같다면 문제 없는 경우 입니다.


Error - LoginPacket .. 관련항목 2개  :  무시합니다.

* Error - Echo Not Recv : 에코를 보냈는데 500 ms 이상 응답이 없는 카운팅 입니다.  이 수치가 누적으로 계속 올라가거나  줄어들지 않는다면  서버에서 패킷 응답이 없거나 손실된 경우 입니다.
일시적으로 수치가 있어도 상관 없으나 계속 커지거나 줄어들지 않으면 문제 있음.

* Error - Packet Error : Echo 보낸것과 응답 받은것이 다름.


위 목록에서 * 이 붙은 항목은 나타나선 안됩니다.



/////////////////////////////////////////////////////////////////////////////////////

헤더 2Byte (길이)
데이터 8Byte (에코)


# 스트레스 클라이언트

1. 임의의 클라이언트 수량만큼 접속 해제를 반복.
2. 각 클라이언트 세션마다 고유하게 증가되는 값 8 Byte 를 서버로 송신
3. 서버로부터 받은 값을 자신이 보냈던 값과 비교.

테스트 항목.

- 접속을 거부하는 경우는 없는가 ?
- 서버가 접속을 끊는 경우가 있는가 ?
- 보낸 값과 되돌아온 값이 일치하는가 ?

- 재접속 없는 상태에서 최소성능 Recv / Send TPS 각각 20만 이상

*/

//#define test_ECHO_EQUAL

class CEchoDummy : public mylib::CLanDummy
{
public:
	CEchoDummy();
	virtual ~CEchoDummy();

	void PrintState();

	

protected:
	void OnClientJoin(UINT64 iSessionID);
	void OnClientLeave(UINT64 iSessionID);
	void OnRecv(UINT64 iSessionID, mylib::CNPacket * pPacket);
	void OnSend(UINT64 iSessionID, int iSendSize);
	void OnError(int iErrCode, WCHAR * wszErr);
	void OnEcho(stSESSION * pSession);

public:
	// Monitor
	
	LONG64	_lWaitEchoCnt;
	LONG64	_lLatencyMax;

	LONG64	_lError_DisconnectFromServerCnt;

	//LONG64	_lError_LoginPacket_Duplication;
	//LONG64	_lError_LoginPacket_NotRecv;
	LONG64	_lError_EchoNotRecv;
	LONG64	_lError_PacketErr;

};
#endif