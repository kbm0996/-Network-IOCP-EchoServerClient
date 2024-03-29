#ifndef __DEFINE_H__
#define __DEFINE_H__

//사용포트 6000
//
//첨부된 프로그램을 실행 하시면
//
//ServerIP : 알아서 입력
//	Disconnec Test : Yes 선택시 무작위로 재접속을 시도 함.
//	Client Count : 테스트 클라이언트 수 입니다.  1 명 / 50 명 / 100 명  선택,
//	OverSendCount : Echo 응답이 오지 않아도 보내는 수치 입니다.
//	기본은 1로 테스트 해주시고 문제가 없다면 늘려보셔도 됩니다.
//
//	Loop Delay ms : 더미 클라이언트의 컨텐츠 루프 딜레이 값 입니다.적으면 적을 수록 에코를 빨리빨리 보냅니다.
//
//
//	실행화면에서 ............................
//
//	Error - Connect Fail : 클라에서 connect 함수가 실패한 경우 입니다.서버가 accept 를 안하거나 빠르게 accept 를 안했을 경우 나타날 수 있음.
//
//	# Error - Disconnect from Server : 서버가 일방적으로 끊은겁니다.지금은 서버가 끊을 상황이 없으므로 동기화 오류로 봐야 합니다.
//
//	Error - LoginPacket ..관련항목 2개 : 무시합니다.
//
//	# Error - Echo Not Recv : 에코를 보냈는데 500 ms 이상 응답이 없는 카운팅 입니다.이 수치가 누적으로 계속 올라가거나  줄어들지 않는다면  서버에서 패킷 응답이 없거나 손실된 경우 입니다.
//	일시적으로 수치가 있어도 상관 없으나 계속 커지거나 줄어들지 않으면 문제 있음.
//
//	# Error - Packet Error : Echo 보낸것과 응답 받은것이 다름.
//
//
//	위 목록에서 # 이 붙은 항목은 나타나선 안됩니다.

#define df_SERVER_IP		L"127.0.0.1"
#define df_SERVER_PORT		6000
#define df_PACKET_SIZE_MAX	1024

#define df_PACKET_PRECODE	0x7fffffffffffffff
#define df_PACKET_TYPE_CHAT	10

#endif // !__DEFINE_H__
