/*---------------------------------------------------------------
  Network Packet (+MemoryPool)
 �޸�Ǯ�� ������ ��Ŷ ���� ����ȭ ����

- Ư¡
 1. ��ȣȭ ���
 2. ��� ���� : �ִ� en_HEADER_SIZE ������ ũ�⸦ ���� ����� ���� ������ �� ����
 3. ���̷ε� ����

- ����
CNPacket::PacketPool(200);				// ��� ���� �ݵ�� ����
CNPacket *pPacket = CNPacket::Alloc();	// �Ҵ�
// ������� �����͸� ����ϴ� ���: PayloadPos���� ���
WORD wHeader = sizeof(UINT64);
UINT64 lData = 0x7fffffffffffffff;
*pPacket << wHeader;
*pPacket << lData;
// ������ ��� ����ϴ� ���: HeaderPos���� ���
*Packet << (WORD)en_PACKET_SS_REQ_NEW_CLIENT_LOGIN;
*Packet << iAccountNo;
Packet->PutData(szSessionKey, 64);
*Packet << iParameter;
WORD wHeader = Packet->GetDataSize();	// ��� ����
Packet->SetHeader_Custom((char*)&wHeader, sizeof(wHeader));
pPacket->Free();						// ����
----------------------------------------------------------------*/
#ifndef  __PACKET_BUFFER__
#define  __PACKET_BUFFER__
#include "CLFMemoryPool_TLS.h"
//#include "CLFMemoryPool.h"
namespace mylib
{
	//  * ����ü ����
	// 1. ��Į�� ����ü�� ����� ���� ũ��� ���� ������ byte padding align�� ����
	// 2. ���� ū ����� byte padding align�� ����
	// 3. ����ü ũ��� ���� ������ ����� offset�� �������� align�Ǵ� byte�� �ּ� ���
	//
	// 1byte ������ ���� ������ sizeof(st_PACKET_HEADER)�� 5Byte�� �ƴ϶� 6Byte�� ��.
	//
	//  - offsetof()�� Ȯ���� ���
	// ����X : 0 2 4 5 // Encode(), Decode() ��Ʈ ���� ���� �� ���� �߻�
	// ����O : 0 1 3 4
#pragma pack(push, 1) 
	struct st_PACKET_HEADER
	{
		BYTE byCode;
		WORD wLen;
		BYTE byRandKey;
		BYTE byCheckSum;
	};
#pragma pack(pop)
	// ��, ������ ���� �������� ���� �ش� �����Ͱ� CPU ĳ�� ����(���μ����� ���� 16~128byte) --i7�� 64bytes ����
	// ��迡 ���������� ĳ�ø� ������ �ؾ��ϹǷ� 3������ ������ �� �ִ�.

	class CNPacket
	{
	public:
		enum en_NETWORK_PACKET
		{
			en_BUFFER_DEFAULT_SIZE = 500,		// ��Ŷ�� �⺻ ���� ũ��.
			en_HEADER_SIZE = 5
		};

		//////////////////////////////////////////////////////////////////////////
		// �޸� �����Ҵ� Alloc(RefCount++), ���� Free(RefCount--)
		// Ŭ���� ��ü�� ����ֵ��� static�� ����, RefCount�� ������������ 1�̴�. 
		//
		// Parameters: ����.
		// Return: (CNPacket*) ���� ������
		//////////////////////////////////////////////////////////////////////////
		static CNPacket* Alloc();
		bool	Free();

		//////////////////////////////////////////////////////////////////////////
		// RefCount�� �����ϰ� ������Ű�� �Լ�
		// Alloc�Ҷ� ����! �Ŀ� ������ �ɶ����� (SendPacket�� ȣ���Ҷ����� ������) AddRef()ȣ��
		//
		// Parameters: ����.
		// Return: (int) ������ RefCount��
		//////////////////////////////////////////////////////////////////////////
		int		AddRef();

		//////////////////////////////////////////////////////////////////////////
		// ��� ���� ��Ŷ ����
		//
		// Parameters: ����.
		// Return: (int) �޸� Ǯ ���� ��ü ����,
		//////////////////////////////////////////////////////////////////////////
		static int GetUseSize();

		//////////////////////////////////////////////////////////////////////////
		// ������, �ı���.
		//
		//////////////////////////////////////////////////////////////////////////
		CNPacket(int iBufferSize = en_BUFFER_DEFAULT_SIZE);// = en_BUFFER_DEFAULT_SIZE);
		virtual ~CNPacket();

		//////////////////////////////////////////////////////////////////////////
		// ��Ŷ �ʱ�ȭ.
		// �޸� �Ҵ��� ���⼭ �ϹǷ�, �Ժη� ȣ���ϸ� �ȵȴ�. 
		//
		// Parameters: (int)BufferSize.
		//////////////////////////////////////////////////////////////////////////
		void	Init(int iBufferSize);


		//////////////////////////////////////////////////////////////////////////
		// ��Ŷ ������
		//
		//////////////////////////////////////////////////////////////////////////
		void	Release();

		//////////////////////////////////////////////////////////////////////////
		// ��Ŷ ��ȣȭ
		// 
		//////////////////////////////////////////////////////////////////////////
		static void SetEncodingCode(BYTE byCode, BYTE byPacketKey1, BYTE byPacketKey2);
		static BYTE GetCode() { return _byCode; }
		void		Encode();
		bool		Decode(st_PACKET_HEADER *pHeader);

		//////////////////////////////////////////////////////////////////////////
		// ��Ŷ û��.
		//
		// Parameters: ����.
		// Return: ����.
		//////////////////////////////////////////////////////////////////////////
		void	Clear();


		//////////////////////////////////////////////////////////////////////////
		// ���� ũ�� ���.
		//
		// Parameters: ����.
		// Return: (int)��Ŷ ���� ũ�� ���.
		//////////////////////////////////////////////////////////////////////////
		int		GetBufferSize() { return _iBufferSize; }

		//////////////////////////////////////////////////////////////////////////
		// ���� ������� ũ�� ���. (���ũ�� ������)
		//
		// Parameters: ����.
		// Return: (int)������� ������ ũ��.
		//////////////////////////////////////////////////////////////////////////
		int		GetDataSize() { return _iDataSize; }

		//////////////////////////////////////////////////////////////////////////
		// ���� ������� ũ�� ���. (���ũ�� ����)
		//
		// Parameters: ����.
		// Return: (int)������� ������ ũ��.
		//////////////////////////////////////////////////////////////////////////
		int		GetPacketSize() { return _iHeaderSize + _iDataSize; }

		//////////////////////////////////////////////////////////////////////////
		// ���� ������ ���.
		//
		// Parameters: ����.
		// Return: (char *)���� ������.
		//////////////////////////////////////////////////////////////////////////
		char*	GetBufferPtr() { return _pBuffer; }

		//////////////////////////////////////////////////////////////////////////
		// ������� ������ ���.
		//
		// Parameters: ����.
		// Return: (char *)���� ������.
		//////////////////////////////////////////////////////////////////////////
		char*	GetHeaderPtr() { return _pHeaderPos; }

		//////////////////////////////////////////////////////////////////////////
		// Payload���� ������ ���.
		//
		// Parameters: ����.
		// Return: (char *)���� ������.
		//////////////////////////////////////////////////////////////////////////
		char*	GetPayloadPtr() { return _pPayloadPos; }

		//////////////////////////////////////////////////////////////////////////
		// ����� �ڵ�(��ŶŸ��) ���.
		//
		// Parameters: ����.
		// Return: (char *)���� ������.
		//////////////////////////////////////////////////////////////////////////
		char*	GetHeader_Code() { return _pHeaderPos; }

		//////////////////////////////////////////////////////////////////////////
		// ����� ���̰� ���.
		//
		// Parameters: ����.
		// Return: (char *)���� ������.
		//////////////////////////////////////////////////////////////////////////
		char*	GetHeader_Len() { return _pHeaderPos + 1; }

		//////////////////////////////////////////////////////////////////////////
		// ����� RandKey�� ���
		//
		// Parameters: ����.
		// Return: (char *)���� ������.
		//////////////////////////////////////////////////////////////////////////
		char*	GetHeader_RandKey() { return _pHeaderPos + 3; }

		//////////////////////////////////////////////////////////////////////////
		// ���� Pos �̵�. (�����̵��� �ȵ�)
		// GetBufferPtr �Լ��� �̿��Ͽ� �ܺο��� ������ ���� ������ ������ ��� ���. 
		//
		// Parameters: (int) �̵� ũ��.
		// Return: (int) �̵��� ũ��.
		//////////////////////////////////////////////////////////////////////////
		int		MoveWritePos(int iSize);
		int		MoveReadPos(int iSize);

		
		/* ============================================================================= */
		// ������ �������̵�
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
		// ������ ���.
		//
		// Parameters: (char *)Dest ������. (int)Size.
		// Return: (int)������ ũ��.
		//////////////////////////////////////////////////////////////////////////
		int		GetData(char *pDest, int iSize);

		//////////////////////////////////////////////////////////////////////////
		// ������ ����.
		//
		// Parameters: (char *)Src ������. (int)SrcSize.
		// Return: (int)������ ũ��.
		//////////////////////////////////////////////////////////////////////////
		int		PutData(char *pSrc, int iSrcSize);

		//////////////////////////////////////////////////////////////////////////
		// ��� ����(����Ʈ ����)
		//
		// Parameters1: (char *)Header ������
		// Return: (int)������ ũ��.
		//////////////////////////////////////////////////////////////////////////
		int		SetHeader(char *pHeader);

		//////////////////////////////////////////////////////////////////////////
		// ��� ����(���� ����)
		//
		// Parameters: (char *)Header ������. (int)HeaderSize.
		// Return: (int)������ ũ��.
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