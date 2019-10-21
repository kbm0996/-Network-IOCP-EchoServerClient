/*---------------------------------------------------------------
  Network Packet (+MemoryPool)
 메모리풀을 내장한 패킷 전용 직렬화 버퍼

- 특징
 1. 암호화 기능
 2. 헤더 삽입 : 최대 en_HEADER_SIZE 이하의 크기를 가진 헤더를 따로 삽입할 수 있음
 3. 페이로드 삽입

- 사용법
CNPacket::PacketPool(200);				// 사용 전에 반드시 선언
CNPacket *pPacket = CNPacket::Alloc();	// 할당
// 헤더없이 데이터만 사용하는 경우: PayloadPos부터 사용
WORD wHeader = sizeof(UINT64);
UINT64 lData = 0x7fffffffffffffff;
*pPacket << wHeader;
*pPacket << lData;
// 임의의 헤더 사용하는 경우: HeaderPos부터 사용
*Packet << (WORD)en_PACKET_SS_REQ_NEW_CLIENT_LOGIN;
*Packet << iAccountNo;
Packet->PutData(szSessionKey, 64);
*Packet << iParameter;
WORD wHeader = Packet->GetDataSize();	// 헤더 설정
Packet->SetHeader_Custom((char*)&wHeader, sizeof(wHeader));
pPacket->Free();						// 해제
----------------------------------------------------------------*/
#ifndef  __PACKET_BUFFER__
#define  __PACKET_BUFFER__
#include "CLFMemoryPool_TLS.h"
//#include "CLFMemoryPool.h"
namespace mylib
{
	//  * 구조체 정렬
	// 1. 스칼라 구조체의 멤버는 변수 크기와 현재 지정된 byte padding align을 따름
	// 2. 가장 큰 멤버의 byte padding align을 따름
	// 3. 구조체 크기는 가장 마지막 멤버의 offset을 기준으로 align되는 byte의 최소 배수
	//
	// 1byte 정렬을 하지 않으면 sizeof(st_PACKET_HEADER)가 5Byte가 아니라 6Byte가 됨.
	//
	//  - offsetof()로 확인한 결과
	// 정렬X : 0 2 4 5 // Encode(), Decode() 비트 연산 수행 시 문제 발생
	// 정렬O : 0 1 3 4
#pragma pack(push, 1) 
	struct st_PACKET_HEADER
	{
		BYTE byCode;
		WORD wLen;
		BYTE byRandKey;
		BYTE byCheckSum;
	};
#pragma pack(pop)
	// 단, 데이터 정렬 변경으로 인해 해당 데이터가 CPU 캐시 라인(프로세서에 따라 16~128byte) --i7은 64bytes 고정
	// 경계에 걸쳐있으면 캐시를 여러번 해야하므로 3배정도 느려질 수 있다.

	class CNPacket
	{
	public:
		enum en_NETWORK_PACKET
		{
			en_BUFFER_DEFAULT_SIZE = 500,		// 패킷의 기본 버퍼 크기.
			en_HEADER_SIZE = 5
		};

		//////////////////////////////////////////////////////////////////////////
		// 메모리 동적할당 Alloc(RefCount++), 해제 Free(RefCount--)
		// 클래스 자체가 들고있도록 static로 선언, RefCount는 생성순간부터 1이다. 
		//
		// Parameters: 없음.
		// Return: (CNPacket*) 버퍼 포인터
		//////////////////////////////////////////////////////////////////////////
		static CNPacket* Alloc();
		bool	Free();

		//////////////////////////////////////////////////////////////////////////
		// RefCount를 안전하게 증가시키는 함수
		// Alloc할때 증가! 후에 참조가 될때마다 (SendPacket을 호출할때마다 그전에) AddRef()호출
		//
		// Parameters: 없음.
		// Return: (int) 증가된 RefCount값
		//////////////////////////////////////////////////////////////////////////
		int		AddRef();

		//////////////////////////////////////////////////////////////////////////
		// 사용 중인 패킷 개수
		//
		// Parameters: 없음.
		// Return: (int) 메모리 풀 내부 전체 개수,
		//////////////////////////////////////////////////////////////////////////
		static int GetUseSize();

		//////////////////////////////////////////////////////////////////////////
		// 생성자, 파괴자.
		//
		//////////////////////////////////////////////////////////////////////////
		CNPacket(int iBufferSize = en_BUFFER_DEFAULT_SIZE);// = en_BUFFER_DEFAULT_SIZE);
		virtual ~CNPacket();

		//////////////////////////////////////////////////////////////////////////
		// 패킷 초기화.
		// 메모리 할당을 여기서 하므로, 함부로 호출하면 안된다. 
		//
		// Parameters: (int)BufferSize.
		//////////////////////////////////////////////////////////////////////////
		void	Init(int iBufferSize);


		//////////////////////////////////////////////////////////////////////////
		// 패킷 릴리즈
		//
		//////////////////////////////////////////////////////////////////////////
		void	Release();

		//////////////////////////////////////////////////////////////////////////
		// 패킷 암호화
		// 
		//////////////////////////////////////////////////////////////////////////
		static void SetEncodingCode(BYTE byCode, BYTE byPacketKey1, BYTE byPacketKey2);
		static BYTE GetCode() { return _byCode; }
		void		Encode();
		bool		Decode(st_PACKET_HEADER *pHeader);

		//////////////////////////////////////////////////////////////////////////
		// 패킷 청소.
		//
		// Parameters: 없음.
		// Return: 없음.
		//////////////////////////////////////////////////////////////////////////
		void	Clear();


		//////////////////////////////////////////////////////////////////////////
		// 버퍼 크기 얻기.
		//
		// Parameters: 없음.
		// Return: (int)패킷 버퍼 크기 얻기.
		//////////////////////////////////////////////////////////////////////////
		int		GetBufferSize() { return _iBufferSize; }

		//////////////////////////////////////////////////////////////////////////
		// 현재 사용중인 크기 얻기. (헤더크기 미포함)
		//
		// Parameters: 없음.
		// Return: (int)사용중인 데이터 크기.
		//////////////////////////////////////////////////////////////////////////
		int		GetDataSize() { return _iDataSize; }

		//////////////////////////////////////////////////////////////////////////
		// 현재 사용중인 크기 얻기. (헤더크기 포함)
		//
		// Parameters: 없음.
		// Return: (int)사용중인 데이터 크기.
		//////////////////////////////////////////////////////////////////////////
		int		GetPacketSize() { return _iHeaderSize + _iDataSize; }

		//////////////////////////////////////////////////////////////////////////
		// 버퍼 포인터 얻기.
		//
		// Parameters: 없음.
		// Return: (char *)버퍼 포인터.
		//////////////////////////////////////////////////////////////////////////
		char*	GetBufferPtr() { return _pBuffer; }

		//////////////////////////////////////////////////////////////////////////
		// 헤더버퍼 포인터 얻기.
		//
		// Parameters: 없음.
		// Return: (char *)버퍼 포인터.
		//////////////////////////////////////////////////////////////////////////
		char*	GetHeaderPtr() { return _pHeaderPos; }

		//////////////////////////////////////////////////////////////////////////
		// Payload버퍼 포인터 얻기.
		//
		// Parameters: 없음.
		// Return: (char *)버퍼 포인터.
		//////////////////////////////////////////////////////////////////////////
		char*	GetPayloadPtr() { return _pPayloadPos; }

		//////////////////////////////////////////////////////////////////////////
		// 헤더의 코드(패킷타입) 얻기.
		//
		// Parameters: 없음.
		// Return: (char *)버퍼 포인터.
		//////////////////////////////////////////////////////////////////////////
		char*	GetHeader_Code() { return _pHeaderPos; }

		//////////////////////////////////////////////////////////////////////////
		// 헤더의 길이값 얻기.
		//
		// Parameters: 없음.
		// Return: (char *)버퍼 포인터.
		//////////////////////////////////////////////////////////////////////////
		char*	GetHeader_Len() { return _pHeaderPos + 1; }

		//////////////////////////////////////////////////////////////////////////
		// 헤더의 RandKey값 얻기
		//
		// Parameters: 없음.
		// Return: (char *)버퍼 포인터.
		//////////////////////////////////////////////////////////////////////////
		char*	GetHeader_RandKey() { return _pHeaderPos + 3; }

		//////////////////////////////////////////////////////////////////////////
		// 버퍼 Pos 이동. (음수이동은 안됨)
		// GetBufferPtr 함수를 이용하여 외부에서 강제로 버퍼 내용을 수정할 경우 사용. 
		//
		// Parameters: (int) 이동 크기.
		// Return: (int) 이동된 크기.
		//////////////////////////////////////////////////////////////////////////
		int		MoveWritePos(int iSize);
		int		MoveReadPos(int iSize);

		
		/* ============================================================================= */
		// 연산자 오버라이딩
		/* ============================================================================= */
		CNPacket	&operator = (CNPacket &clSrcCNPacket);

		CNPacket	&operator << (BYTE byValue);
		CNPacket	&operator << (char chValue);
		CNPacket	&operator << (short shValue);
		CNPacket	&operator << (WORD wValue);
		CNPacket	&operator << (int iValue);
		CNPacket	&operator << (DWORD dwValue);
		CNPacket	&operator << (float fValue);
		CNPacket	&operator << (__int64 iValue);
		CNPacket	&operator << (double dValue);
		CNPacket	&operator << (UINT uiValue);
		CNPacket	&operator << (UINT64 uiValue);

		CNPacket	&operator >> (BYTE &byValue);
		CNPacket	&operator >> (char &chValue);
		CNPacket	&operator >> (short &shValue);
		CNPacket	&operator >> (WORD &wValue);
		CNPacket	&operator >> (int &iValue);
		CNPacket	&operator >> (DWORD &dwValue);
		CNPacket	&operator >> (float &fValue);
		CNPacket	&operator >> (__int64 &iValue);
		CNPacket	&operator >> (double &dValue);
		CNPacket	&operator >> (UINT &uiValue);
		CNPacket	&operator >> (UINT64 &uiValue);

		//////////////////////////////////////////////////////////////////////////
		// 데이터 얻기.
		//
		// Parameters: (char *)Dest 포인터. (int)Size.
		// Return: (int)복사한 크기.
		//////////////////////////////////////////////////////////////////////////
		int		GetData(char *pDest, int iSize);

		//////////////////////////////////////////////////////////////////////////
		// 데이터 삽입.
		//
		// Parameters: (char *)Src 포인터. (int)SrcSize.
		// Return: (int)복사한 크기.
		//////////////////////////////////////////////////////////////////////////
		int		PutData(char *pSrc, int iSrcSize);

		//////////////////////////////////////////////////////////////////////////
		// 헤더 삽입(바이트 고정)
		//
		// Parameters1: (char *)Header 포인터
		// Return: (int)복사한 크기.
		//////////////////////////////////////////////////////////////////////////
		int		SetHeader(char *pHeader);

		//////////////////////////////////////////////////////////////////////////
		// 헤더 삽입(길이 설정)
		//
		// Parameters: (char *)Header 포인터. (int)HeaderSize.
		// Return: (int)복사한 크기.
		//////////////////////////////////////////////////////////////////////////
		int		SetHeader_Custom(char *pHeader, int iHeaderSize);

	public:
		// PACKET
	#ifdef __MEMORY_POOL_TLS__
		static CLFMemoryPool_TLS<CNPacket> _PacketPool;
	#elif defined __LF_MEMORY_POOL__
		static CLFMemoryPool<CNPacket> _PacketPool;
	#endif

	protected:
		char*	_pBuffer;
		int		_iBufferSize;	// Header Size + Payload Size
		LONG	_lRefCount;
		// PACKET - HEADER, PAYLOAD POSITION
		char*	_pReadPos;
		char*	_pWritePos;
		char*	_pHeaderPos;
		char*	_pPayloadPos;
		int		_iDataSize;		// Payload Size
		// HEADER
		int		_iHeaderSize;
		// Encoding
		bool		_bEncoding;
		static BYTE	_byCode;
		static BYTE	_byPacketKey1;
		static BYTE	_byPacketKey2;
	};

}
#endif