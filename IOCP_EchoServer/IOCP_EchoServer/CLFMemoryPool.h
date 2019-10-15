//---------------------------------------------------------------
//  Lockfree MemoryPool
//
// ������ �˰����� �̿��� ����ȭ ��ü���� ������ ������ �޸�Ǯ
//
//
// - ����ȭ ������ ���
//
//  ���� ȯ�濡 ���� ����ȭ �������� �� ���� ����.
// �ڵ忡 ��ȭ�� ���� �߻��ϹǷ� ����� �۵��� �� �׽�Ʈ�� �غ�����.
// �ٸ�, �����ϸ� volatile �������� �ذ��
//
//
// - ����
//
// //DATA *pData = MemPool.Alloc();
//
// //~ pData ��� ~
//
// //MemPool.Free(pDATA);
//----------------------------------------------------------------
#ifndef  __LF_MEMORY_POOL__
#define  __LF_MEMORY_POOL__
#include <Windows.h>
#include <assert.h>
#include <new.h>

namespace mylib
{
	template <class DATA>
	class CLFMemoryPool
	{
	private:
		/* ******************************************************************** */
		// �� �� �տ� ���� ��� ����ü.
		/* ******************************************************************** */
		//  st_BLOCK_NODE�� DATA data;�� ���� ���� �ʰ� ����ó�� ���Ƿ� ���������
		// �޸� ĳ�ö����� �ȸ¾Ƽ� ��Ʈ�� �ɰ����� ��� �� ������ �߻��� �� ����.
		struct st_BLOCK_NODE
		{
			st_BLOCK_NODE*	pNextBlock;
		};

		struct st_TOP_NODE
		{
			st_BLOCK_NODE*	pTopBlock;
			__int64			iUnique;
		};

	public:
		//////////////////////////////////////////////////////////////////////////
		// ������, �ı���.
		//
		// Parameters:	(int) �ִ� ��� ���� (0���� ������ �������� �Ҵ�)
		//				(bool) ������ ȣ�� ����
		// Return:
		//////////////////////////////////////////////////////////////////////////
		CLFMemoryPool(int iBlockCnt = 0, bool bPlacementNew = false);
		virtual	~CLFMemoryPool() { _aligned_free(_pTop); }


		//////////////////////////////////////////////////////////////////////////
		// �� �ϳ��� �Ҵ�޴´�.
		//
		// Parameters: ����.
		// Return: (DATA *) ������ �� ������.
		//////////////////////////////////////////////////////////////////////////
		DATA* Alloc();

		//////////////////////////////////////////////////////////////////////////
		// ������̴� ���� �����Ѵ�.
		//
		// Parameters: (DATA *) �� ������.
		// Return: (BOOL) TRUE, FALSE.
		//////////////////////////////////////////////////////////////////////////
		bool Free(DATA* pOutData);

		//////////////////////////////////////////////////////////////////////////
		// ���� Ȯ�� �� �� ������ ��´�. (�޸�Ǯ ������ ��ü ����)
		//
		// Parameters: ����.
		// Return: (int) �޸� Ǯ ���� ��ü ����
		//////////////////////////////////////////////////////////////////////////
		int	GetAllocSize() { return (int)_lAllocSize; }

		//////////////////////////////////////////////////////////////////////////
		// ���� ������� �� ������ ��´�.
		//
		// Parameters: ����.
		// Return: (int) ������� �� ����.
		//////////////////////////////////////////////////////////////////////////
		int	GetUseSize() { return (int)_lUseSize; }

	private:
		//////////////////////////////////////////////////////////////////////////
		// NODE
		st_TOP_NODE* _pTop;
		__int64		_iUnique;		// Top �ĺ�, ABA ���� ����

									//////////////////////////////////////////////////////////////////////////
									// MONITOR
		LONG64		_lUseSize;
		LONG64		_lAllocSize;

		//////////////////////////////////////////////////////////////////////////
		// OPTION
		bool		_bFreelist;		// ��� ���� �Ҵ� ����(�ʱ� AllocSize == 0 �� ���)
		bool		_bPlacementNew;
	};


	template<class DATA>
	CLFMemoryPool<DATA>::CLFMemoryPool(int iBlockCnt = 0, bool bPlacementNew = false)
	{
		_lAllocSize = iBlockCnt;
		_lUseSize = 0;
		_bPlacementNew = bPlacementNew;
		_iUnique = 0;

		if (iBlockCnt <= 0)
			_bFreelist = true;
		else
			_bFreelist = false;

		// InterlockedCompareExchange128�� ù��° ���ڴ� ���� �ּҰ� 16����Ʈ ���ĵǾ��־�� ��� ����
		_pTop = (st_TOP_NODE *)_aligned_malloc(sizeof(st_TOP_NODE), 16);
		_pTop->pTopBlock = nullptr;
		_pTop->iUnique = 0;

		if (!_bFreelist)
		{
			st_BLOCK_NODE* pNode = (st_BLOCK_NODE*)malloc((sizeof(st_BLOCK_NODE) + sizeof(DATA))*iBlockCnt);
			for (int i = 0; i < iBlockCnt; ++i)
				pNode[i].pNextBlock = &pNode[i + 1];
			_pTop->pTopBlock = pNode;
		}
	}

	template<class DATA>
	DATA* CLFMemoryPool<DATA>::Alloc()
	{
		st_BLOCK_NODE* pBlock_New = nullptr;
		volatile st_TOP_NODE	stTopNode_Copy;

		DATA* pOutData;

		LONG64 lAllocCount = _lAllocSize;

		// �� ��
		if (lAllocCount < InterlockedIncrement64(&_lUseSize))
		{
			if (!_bFreelist)
			{
				InterlockedDecrement64(&_lUseSize);
				return nullptr;
			}

			// �ű� �Ҵ�
			pBlock_New = (st_BLOCK_NODE *)malloc(sizeof(st_BLOCK_NODE) + sizeof(DATA));
			InterlockedIncrement64(&_lAllocSize);

			pOutData = (DATA *)(pBlock_New + 1);
			new (pOutData)DATA;
			return pOutData;
		}
		else
		{
			__int64 iUnique = InterlockedIncrement64(&_iUnique);

			do
			{
				stTopNode_Copy.iUnique = _pTop->iUnique;
				stTopNode_Copy.pTopBlock = _pTop->pTopBlock;
			} while (!InterlockedCompareExchange128(
				(LONG64 *)_pTop,
				iUnique, (LONG64)_pTop->pTopBlock->pNextBlock,
				(LONG64 *)&stTopNode_Copy
			));

			pBlock_New = stTopNode_Copy.pTopBlock;
		}

		pOutData = (DATA *)(pBlock_New + 1);

		if (_bPlacementNew)
			new (pOutData)DATA;

		return pOutData;
	}

	template<class DATA>
	bool CLFMemoryPool<DATA>::Free(DATA* pOutData)
	{
		volatile st_BLOCK_NODE*	pNode = ((st_BLOCK_NODE *)pOutData) - 1;
		volatile st_TOP_NODE	stTopNode_Copy;

		__int64 iUnique = InterlockedIncrement64(&_iUnique);

		do
		{
			stTopNode_Copy.iUnique = _pTop->iUnique;
			stTopNode_Copy.pTopBlock = _pTop->pTopBlock;

			pNode->pNextBlock = _pTop->pTopBlock;
		} while (!InterlockedCompareExchange128(
			(LONG64*)_pTop,
			iUnique, (LONG64)pNode,
			(LONG64*)&stTopNode_Copy
		));

		if (_bPlacementNew)
			pOutData->~DATA();

		InterlockedDecrement64(&_lUseSize);

		return true;
	}
}
#endif