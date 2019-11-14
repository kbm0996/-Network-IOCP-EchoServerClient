#ifndef __NETWORK_H__
#define __NETWORK_H__

#include "_Define.h"
#include "CLanDummy.h"
#include <list>
#include <map>

/*
CLanServer �߰� �׽�Ʈ.

LanServer EchoDummy Port : 6000

# ����

ServerIP :  �˾Ƽ� �Է�
Disconnec Test :  Yes ���ý� �������� �������� �õ� ��.
Client Count :  �׽�Ʈ Ŭ���̾�Ʈ �� �Դϴ�.  1 �� / 50 �� / 100 ��  ����,
OverSendCount :  Echo ������ ���� �ʾƵ� ������ ��ġ �Դϴ�.
�⺻�� 1�� �׽�Ʈ ���ֽð� ������ ���ٸ� �ø��ϴ�.

Loop Delay ms :  ���� Ŭ���̾�Ʈ�� ������ ���� ������ �� �Դϴ�.
������ ���� ���� ���ڸ� �������� �����ϴ�.


# ����ȭ�鿡��

Error - Connect Fail  :  Ŭ�󿡼� connect �Լ��� ������ ��� �Դϴ�.  ������ accept �� ���ϰų� ������ accept �� ������ ��� ��Ÿ�� �� ����.

* Error - Disconnect from Server : ������ �Ϲ������� �����̴ϴ�.  ������ ������ ���� ��Ȳ�� �����Ƿ� ����ȭ ������ ���� �մϴ�.
�� ��ġ�� �߻��ϴ� ���� ������ AcceptTotal �� ������ ConnectTotal ��ġ�� �� �մϴ�.
Total ���� ���ٸ� ���� ���� ��� �Դϴ�.


Error - LoginPacket .. �����׸� 2��  :  �����մϴ�.

* Error - Echo Not Recv : ���ڸ� ���´µ� 500 ms �̻� ������ ���� ī���� �Դϴ�.  �� ��ġ�� �������� ��� �ö󰡰ų�  �پ���� �ʴ´ٸ�  �������� ��Ŷ ������ ���ų� �սǵ� ��� �Դϴ�.
�Ͻ������� ��ġ�� �־ ��� ������ ��� Ŀ���ų� �پ���� ������ ���� ����.

* Error - Packet Error : Echo �����Ͱ� ���� �������� �ٸ�.


�� ��Ͽ��� * �� ���� �׸��� ��Ÿ���� �ȵ˴ϴ�.



/////////////////////////////////////////////////////////////////////////////////////

��� 2Byte (����)
������ 8Byte (����)


# ��Ʈ���� Ŭ���̾�Ʈ

1. ������ Ŭ���̾�Ʈ ������ŭ ���� ������ �ݺ�.
2. �� Ŭ���̾�Ʈ ���Ǹ��� �����ϰ� �����Ǵ� �� 8 Byte �� ������ �۽�
3. �����κ��� ���� ���� �ڽ��� ���´� ���� ��.

�׽�Ʈ �׸�.

- ������ �ź��ϴ� ���� ���°� ?
- ������ ������ ���� ��찡 �ִ°� ?
- ���� ���� �ǵ��ƿ� ���� ��ġ�ϴ°� ?

- ������ ���� ���¿��� �ּҼ��� Recv / Send TPS ���� 20�� �̻�

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