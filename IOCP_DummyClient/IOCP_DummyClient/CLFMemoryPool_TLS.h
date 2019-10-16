/*---------------------------------------------------------------
  MemoryPool TLS

 TLS(Thread Local Storage)�� Ȱ���� �ϳ��� �޸�Ǯ�κ��� �����庰�� ������ ûũ �Ҵ�
 ���μ����� TLS ������ ���ѵǾ������Ƿ� ���� �����ؾ� ��

 - Ŭ����
 * CLFMemoryPool_TLS: CDataChunk�� �����ϴ� �޸�Ǯ�� ������ Ŭ���� 
 * CDataChunk		: DATA Block ���. ��Ȱ��X. 

 - ����
 CLFMemoryPool_TLS<DATA> MemPool(300, FALSE);
 DATA *pData = MemPool.Alloc();
 ~ pData ��� ~
 MemPool.Free(pDATA);
----------------------------------------------------------------*/
#ifndef  __MEMORY_POOL_TLS__
#define  __MEMORY_POOL_TLS__
#include "CLFMemoryPool.h"
#include "CSystemLog.h"
#include "CrashDump.h"

#define df_CHECK_CODE 0x12345ABCDE6789F0

namespace mylib
{
//#define DATA int
	template<class DATA>
	class CLFMemoryPool_TLS
	{
	private:
		class CChunk;
		struct st_CHUNK_NODE
		{
			CChunk* pChunk;
			UINT64 iCheck;
			DATA Data;
		};

	public:
		//////////////////////////////////////////////////////////////////////////
		// ������, �ı���.
		//
		// Parameters:	(int) �ִ� �� ����.
		//				(bool) ������ ȣ�� ����.
		// Return:
		//////////////////////////////////////////////////////////////////////////
		CLFMemoryPool_TLS(LONG lChunkSize = 3000);
		virtual ~CLFMemoryPool_TLS();

		//////////////////////////////////////////////////////////////////////////
		// CHUNK �Ҵ�, ����
		//
		// Parameters: ����.
		// Return: (DATA *) ������ �� ������.
		//////////////////////////////////////////////////////////////////////////
		DATA*	Alloc();
		bool	Free(DATA *pData);

	private:
		//////////////////////////////////////////////////////////////////////////
		// CHUNK �Ҵ�
		//
		// Parameters: ����.
		// Return: (DATA *) ������ �� ������.
		//////////////////////////////////////////////////////////////////////////
		CChunk*	ChunkAlloc();

	public:
		//////////////////////////////////////////////////////////////////////////
		// ��� ���� �� ����
		//
		// Parameters: ����.
		// Return: (int) �޸� Ǯ ���� ��ü ����,
		//////////////////////////////////////////////////////////////////////////
		int	GetUseSize() { return _lUseCnt; }

		//////////////////////////////////////////////////////////////////////////
		// Ȯ���� ûũ ����
		//
		// Parameters: ����.
		// Return: (int) �޸� Ǯ ���� ��ü ����,
		//////////////////////////////////////////////////////////////////////////
		int	GetAllocSize() { return _pChunkPool->GetAllocSize(); } 

	private:
		// TLS
		DWORD	_dwTlsIndex;
		// CHUNK
		CLFMemoryPool<CChunk>* _pChunkPool;
		LONG	_lChunkSize;	// ûũ ũ��
		// MONITOR
		LONG	_lUseCnt;		// ������� �� ��

	private:
		class CChunk
		{
		public:
			//////////////////////////////////////////////////////////////////////////
			// ������, �ı���.
			//
			// Parameters:	
			// Return:
			//////////////////////////////////////////////////////////////////////////
			CChunk();
			virtual	~CChunk() { delete[] _pDataNode; }

			void	Init(CLFMemoryPool_TLS<DATA>* pMemoryPool, LONG lChunkSize);
			void	Clear(LONG lChunkSize);

			DATA*	Alloc();
			bool	Free();

		private:
			friend class CLFMemoryPool_TLS<DATA>;
			CLFMemoryPool_TLS<DATA>*	_pMemoryPoolTLS;

			// BLOCK
			st_CHUNK_NODE*	_pDataNode;	// ������ ��

			LONG	_lChunkSize;	// ûũ �� �� ���� (�ִ� �� ����)
			LONG	_lAllocIndex;	// ������� �� ���� (�ε���)
			LONG 	_iFreeCount;	// 0�� �Ǹ� Free
		};
	};


	template<class DATA>
	CLFMemoryPool_TLS<DATA>::CLFMemoryPool_TLS(LONG lChunkSize)
	{
		_dwTlsIndex = TlsAlloc();
		if (_dwTlsIndex == TLS_OUT_OF_INDEXES)
			CRASH();

		_pChunkPool = new CLFMemoryPool<CChunk>();
		_lChunkSize = lChunkSize;
		_lUseCnt = 0;
	}

	template<class DATA>
	CLFMemoryPool_TLS<DATA>::~CLFMemoryPool_TLS()
	{
		TlsFree(_dwTlsIndex);
		delete[] _pChunkPool;
	}

	template<class DATA>
	DATA* CLFMemoryPool_TLS<DATA>::Alloc()
	{
		CChunk* pChunk = (CChunk*)TlsGetValue(_dwTlsIndex);
		if (pChunk == nullptr)
			pChunk = ChunkAlloc();

		InterlockedIncrement(&_lUseCnt);

		return  pChunk->Alloc();
	}

	template<class DATA>
	bool CLFMemoryPool_TLS<DATA>::Free(DATA *pData)
	{
		st_CHUNK_NODE* pNode = (st_CHUNK_NODE*)((char*)pData - (sizeof(st_CHUNK_NODE::pChunk) + sizeof(st_CHUNK_NODE::iCheck)));
		if (pNode->iCheck != df_CHECK_CODE)
			CRASH();

		InterlockedDecrement(&_lUseCnt);

		pNode->pChunk->Free();
		return true;
	}

	template<class DATA>
	typename CLFMemoryPool_TLS<DATA>::CChunk * CLFMemoryPool_TLS<DATA>::ChunkAlloc()
	{
		CChunk* pChunk = _pChunkPool->Alloc();

		if (pChunk->_pDataNode == nullptr)
		{
			pChunk->Init(this, _lChunkSize);
		}
		else
		{
			pChunk->Clear(_lChunkSize);
		}
		
		TlsSetValue(_dwTlsIndex, pChunk);

		return pChunk;
	}

	template<class DATA>
	CLFMemoryPool_TLS<DATA>::CChunk::CChunk()
	{
		_pMemoryPoolTLS = nullptr;
		_pDataNode = nullptr;
		_lChunkSize = 0;
		_lAllocIndex = 0;
		_iFreeCount = 0;
	}

	template<class DATA>
	void CLFMemoryPool_TLS<DATA>::CChunk::Init(CLFMemoryPool_TLS<DATA>* pMemoryPool, LONG lChunkSize)
	{
		_pMemoryPoolTLS = pMemoryPool;
		_lChunkSize = lChunkSize;
		_iFreeCount = lChunkSize;

		_pDataNode = new st_CHUNK_NODE[_lChunkSize];
		for (int i = 0; i < lChunkSize; ++i)
		{
			_pDataNode[i].pChunk = this;
			_pDataNode[i].iCheck = df_CHECK_CODE;
		}
	}

	template<class DATA>
	void CLFMemoryPool_TLS<DATA>::CChunk::Clear(LONG lChunkSize)
	{
		_lAllocIndex = 0;
		_iFreeCount = lChunkSize;
	}


	template<class DATA>
	DATA* CLFMemoryPool_TLS<DATA>::CChunk::Alloc()
	{
		DATA* pData = &_pDataNode[_lAllocIndex].Data;

		if (_lChunkSize == InterlockedIncrement(&_lAllocIndex))
			_pMemoryPoolTLS->ChunkAlloc();

		return pData;
	}

	template<class DATA>
	bool CLFMemoryPool_TLS<DATA>::CChunk::Free()
	{
		if (0 == InterlockedDecrement(&_iFreeCount))
		{
			_pMemoryPoolTLS->_pChunkPool->Free(_pDataNode->pChunk);

			return true;
		}
		return false;
	}


}
#endif