#include "CNPacket.h"
#include "CrashDump.h"

using namespace mylib;

//  static ������ ���� ����ó�� ��ü ���� ������ ������(data) ������ �Ҵ�
// ����, �����ڿ��� �ʱ�ȭ�� �Ұ����ϹǷ� ������ �������� �ʱ�ȭ�������

#ifdef __MEMORY_POOL_TLS__
CLFMemoryPool_TLS<CNPacket> CNPacket::_PacketPool;
#elif defined __LF_MEMORY_POOL__
CLFMemoryPool<CNPacket> CNPacket::_PacketPool;
#endif

BYTE CNPacket::_byPacketKey1 = 0;
BYTE CNPacket::_byPacketKey2 = 0;
BYTE CNPacket::_byCode = 0;

CNPacket::CNPacket(int iBufferSize)
{
	Init(iBufferSize);
}

CNPacket::~CNPacket()
{
	Release();
}

void CNPacket::Init(int iBufferSize)
{
	_iBufferSize = iBufferSize;
	_pBuffer = new char[iBufferSize];

	_pHeaderPos = _pBuffer;
	_pPayloadPos = _pBuffer + en_HEADER_SIZE;

	_pReadPos = _pBuffer + en_HEADER_SIZE;
	_pWritePos = _pBuffer + en_HEADER_SIZE;

	_iDataSize = 0;
	_iHeaderSize = en_HEADER_SIZE;

	_lRefCount = 0;
	_bEncoding = false;
}

void CNPacket::Release()
{
	if (_pBuffer != nullptr)
		delete[] _pBuffer;
	_pBuffer = nullptr;
}

void CNPacket::Clear()
{
	_pHeaderPos = _pBuffer;
	_pPayloadPos = _pBuffer + en_HEADER_SIZE;

	_pReadPos = _pBuffer + en_HEADER_SIZE;
	_pWritePos = _pBuffer + en_HEADER_SIZE;

	_iDataSize = 0;
	_iHeaderSize = en_HEADER_SIZE;

	_lRefCount = 0;
	_bEncoding = false;
}

int CNPacket::MoveWritePos(int iSize)
{
	if (iSize < 0)
		return 0;

	// ���� �ʰ�
	if (_pWritePos + iSize > _pBuffer + _iBufferSize)
		return 0;

	_pWritePos = _pWritePos + iSize;
	_iDataSize += iSize;

	return iSize;
}

int CNPacket::MoveReadPos(int iSize)
{
	if (iSize < 0)
		return 0;

	// �������
	if (iSize > _iDataSize)
		return 0;

	_pReadPos = _pReadPos + iSize;
	_iDataSize -= iSize;

	return iSize;
}

CNPacket * mylib::CNPacket::Alloc()
{
	CNPacket* pPacket = _PacketPool.Alloc();
	pPacket->Clear();
	InterlockedIncrement(&pPacket->_lRefCount);
	return pPacket;
}

bool mylib::CNPacket::Free()
{
	if (InterlockedDecrement(&_lRefCount) == 0)
	{
		_PacketPool.Free(this);
		return true;
	}
	return false;
}

int mylib::CNPacket::GetUseSize()
{
	return _PacketPool.GetUseSize();
}

int mylib::CNPacket::AddRef()
{
	return InterlockedIncrement(&_lRefCount);
}

CNPacket & CNPacket::operator=(CNPacket & clSrcCNPacket)
{
	_pHeaderPos = _pBuffer;
	_pPayloadPos = _pBuffer + en_HEADER_SIZE;
	_pReadPos = _pBuffer + en_HEADER_SIZE;
	_pWritePos = _pBuffer + en_HEADER_SIZE;

	SetHeader(clSrcCNPacket._pBuffer);
	PutData(clSrcCNPacket._pPayloadPos, clSrcCNPacket._iDataSize);
	return *this;
}

CNPacket & CNPacket::operator<<(BYTE byValue)
{
	PutData((char*)&byValue, sizeof(BYTE));
	return *this;
}

CNPacket & CNPacket::operator<<(char chValue)
{
	PutData(&chValue, sizeof(char));
	return *this;
}

CNPacket & CNPacket::operator<<(short shValue)
{
	PutData((char*)&shValue, sizeof(short));
	return *this;
}

CNPacket & CNPacket::operator<<(WORD wValue)
{
	PutData((char*)&wValue, sizeof(WORD));
	return *this;
}

CNPacket & CNPacket::operator<<(int iValue)
{
	PutData((char*)&iValue, sizeof(int));
	return *this;
}

CNPacket & CNPacket::operator<<(DWORD dwValue)
{
	PutData((char*)&dwValue, sizeof(DWORD));
	return *this;
}

CNPacket & CNPacket::operator<<(float fValue)
{
	PutData((char*)&fValue, sizeof(float));
	return *this;
}

CNPacket & CNPacket::operator<<(__int64 iValue)
{
	PutData((char*)&iValue, sizeof(__int64));
	return *this;
}

CNPacket & CNPacket::operator<<(double dValue)
{
	PutData((char*)&dValue, sizeof(double));
	return *this;
}

CNPacket & CNPacket::operator<<(UINT uiValue)
{
	PutData((char*)&uiValue, sizeof(UINT));
	return *this;
}

CNPacket & CNPacket::operator<<(UINT64 uiValue)
{
	PutData((char*)&uiValue, sizeof(UINT64));
	return *this;
}

CNPacket & CNPacket::operator >> (BYTE & byValue)
{
	GetData((char*)&byValue, sizeof(BYTE));
	return *this;
}

CNPacket & CNPacket::operator >> (char & chValue)
{
	GetData(&chValue, sizeof(char));
	return *this;
}

CNPacket & CNPacket::operator >> (short & shValue)
{
	GetData((char*)&shValue, sizeof(short));
	return *this;
}

CNPacket & CNPacket::operator >> (WORD & wValue)
{
	GetData((char*)&wValue, sizeof(WORD));
	return *this;
}

CNPacket & CNPacket::operator >> (int & iValue)
{
	GetData((char*)&iValue, sizeof(int));
	return *this;
}

CNPacket & CNPacket::operator >> (DWORD & dwValue)
{
	GetData((char*)&dwValue, sizeof(DWORD));
	return *this;
}

CNPacket & CNPacket::operator >> (float & fValue)
{
	GetData((char*)&fValue, sizeof(float));
	return *this;
}

CNPacket & CNPacket::operator >> (__int64 & iValue)
{
	GetData((char*)&iValue, sizeof(__int64));
	return *this;
}

CNPacket & CNPacket::operator >> (double & dValue)
{
	GetData((char*)&dValue, sizeof(double));
	return *this;
}

CNPacket & CNPacket::operator >> (UINT & uiValue)
{
	GetData((char*)&uiValue, sizeof(UINT));
	return *this;
}

CNPacket & CNPacket::operator >> (UINT64 & uiValue)
{
	GetData((char*)&uiValue, sizeof(UINT64));
	return *this;
}

int CNPacket::GetData(char * pDest, int iSize)
{
	if (_iDataSize < iSize)
		return 0;

	memcpy(pDest, _pReadPos, iSize);
	_pReadPos += iSize;
	_iDataSize -= iSize;

	return iSize;
}

int CNPacket::PutData(char * pSrc, int iSrcSize)
{
	if (_pWritePos + iSrcSize > _pBuffer + _iBufferSize)
		return 0;

	memcpy(_pWritePos, pSrc, iSrcSize);
	_pWritePos += iSrcSize;
	_iDataSize += iSrcSize;

	return iSrcSize;
}

int CNPacket::SetHeader(char * pHeader)
{
	memcpy(_pHeaderPos, pHeader, en_HEADER_SIZE);
	return en_HEADER_SIZE;
}

int CNPacket::SetHeader_Custom(char * pHeader, int iHeaderSize)
{
	_pHeaderPos = (_pHeaderPos + en_HEADER_SIZE - iHeaderSize);
	memcpy(_pHeaderPos, pHeader, iHeaderSize);
	_iHeaderSize = iHeaderSize;
	return iHeaderSize;
}

void mylib::CNPacket::SetEncodingCode(BYTE byCode, BYTE byPacketKey1, BYTE byPacketKey2)
{
	_byCode = byCode;
	_byPacketKey1 = byPacketKey1;
	_byPacketKey2 = byPacketKey2;
}

void mylib::CNPacket::Encode()
{
	if (_bEncoding)
		return;

	_bEncoding = true;

	// ����Ű1 XOR ����Ű2
	BYTE byKey = _byPacketKey1 ^ _byPacketKey2;

	// ����Ű ����
	BYTE byKeyRand = rand() % 256;

	BYTE *pPacketBuffer = (BYTE *)GetPayloadPtr();
	int iPayloadSize = GetDataSize();

	// 1. CheckSum  ����
	BYTE byCheckSum;
	int iCheckSum = 0;
	for (int iCnt = 0; iCnt < iPayloadSize; ++iCnt)
	{
		iCheckSum += pPacketBuffer[iCnt];
	}
	byCheckSum = iCheckSum % 256;

	// 2. CheckSum + Payload  ����Ű XOR
	byCheckSum ^= byKeyRand;
	for (int iCnt = 0; iCnt < iPayloadSize; ++iCnt)
	{
		pPacketBuffer[iCnt] ^= byKeyRand;
	}

	// 3. ����Ű�� [ Rand XOR Code - CheckSum - Payload ] �� XOR
	byKeyRand ^= byKey;
	byCheckSum ^= byKey;

	for (int iCnt = 0; iCnt < iPayloadSize; ++iCnt)
	{
		pPacketBuffer[iCnt] ^= byKey;
	}

	st_PACKET_HEADER *pHeader = (st_PACKET_HEADER*)GetHeaderPtr();
	pHeader->byCode = _byCode;
	pHeader->wLen = GetDataSize();
	pHeader->byRandKey = byKeyRand;
	pHeader->byCheckSum = byCheckSum;
}

bool mylib::CNPacket::Decode(st_PACKET_HEADER *pHeader)
{
	// ����Ű1 XOR ����Ű2
	BYTE	byXORKey = _byPacketKey1 ^ _byPacketKey2; // 182 50 132
	BYTE*	pPacketHeader = (BYTE*)GetPayloadPtr();
	int		iDataSize = GetDataSize();

	// 2. RecvRandKey, RecvChckSum, Payload  ����Ű ��ȣȭ
	pHeader->byRandKey ^= byXORKey;	// 118 182
	pHeader->byCheckSum ^= byXORKey; // 128 182
	for (int iCnt = 0; iCnt < iDataSize; ++iCnt)
	{
		pPacketHeader[iCnt] ^= byXORKey;
	}

	// 3. ����Ű�� CheckSum, Payload ��ȣȭ
	pHeader->byCheckSum ^= pHeader->byRandKey;

	for (int iCnt = 0; iCnt < iDataSize; ++iCnt)
	{
		pPacketHeader[iCnt] ^= pHeader->byRandKey;
	}

	// 4. CheckSum ���
	int iCheckSum = 0;
	for (int iCnt = 0; iCnt < iDataSize; ++iCnt)
	{
		iCheckSum += pPacketHeader[iCnt];
	}

	BYTE iCheck = (BYTE)iCheckSum % 256;
	if (pHeader->byCheckSum != iCheckSum % 256)
		return false;
	return  true;
}