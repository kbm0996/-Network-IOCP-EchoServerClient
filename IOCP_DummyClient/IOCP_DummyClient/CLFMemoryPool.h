//---------------------------------------------------------------
//  Lockfree MemoryPool
//
// 락프리 알고리즘을 이용한 동기화 객체없이 스레드 안전한 메모리풀
//
//
// - 최적화 컴파일 대비
//
//  개발 환경에 따라서 최적화 컴파일을 할 수도 있음.
// 코드에 변화가 많이 발생하므로 제대로 작동할 지 테스트를 해봐야함.
// 다만, 웬만하면 volatile 선언으로 해결됨
//
//
// - 사용법
//
// //DATA *pData = MemPool.Alloc();
//
// //~ pData 사용 ~
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
		// 각 블럭 앞에 사용될 노드 구조체.
		/* ******************************************************************** */
		//  st_BLOCK_NODE에 DATA data;를 따로 두지 않고 다음처럼 임의로 계산했을때
		// 메모리 캐시라인이 안맞아서 비트가 쪼개져서 얻는 등 문제가 발생할 수 있음.
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
		// 생성자, 파괴자.
		//
		// Parameters:	(int) 최대 블록 개수 (0으로 지정시 동적으로 할당)
		//				(bool) 생성자 호출 여부
		// Return:
		//////////////////////////////////////////////////////////////////////////
		CLFMemoryPool(int iBlockCnt = 0, bool bPlacementNew = false);
		virtual	~CLFMemoryPool() { _aligned_free(_pTop); }


		//////////////////////////////////////////////////////////////////////////
		// 블럭 하나를 할당받는다.
		//
		// Parameters: 없음.
		// Return: (DATA *) 데이터 블럭 포인터.
		//////////////////////////////////////////////////////////////////////////
		DATA* Alloc();

		//////////////////////////////////////////////////////////////////////////
		// 사용중이던 블럭을 해제한다.
		//
		// Parameters: (DATA *) 블럭 포인터.
		// Return: (BOOL) TRUE, FALSE.
		//////////////////////////////////////////////////////////////////////////
		bool Free(DATA* pOutData);

		//////////////////////////////////////////////////////////////////////////
		// 현재 확보 된 블럭 개수를 얻는다. (메모리풀 내부의 전체 개수)
		//
		// Parameters: 없음.
		// Return: (int) 메모리 풀 내부 전체 개수
		//////////////////////////////////////////////////////////////////////////
		int	GetAllocSize() { return (int)_lAllocSize; }

		//////////////////////////////////////////////////////////////////////////
		// 현재 사용중인 블럭 개수를 얻는다.
		//
		// Parameters: 없음.
		// Return: (int) 사용중인 블럭 개수.
		//////////////////////////////////////////////////////////////////////////
		int	GetUseSize() { return (int)_lUseSize; }

	private:
		//////////////////////////////////////////////////////////////////////////
		// NODE
		st_TOP_NODE* _pTop;
		__int64		_iUnique;		// Top 식별, ABA 문제 방지

									//////////////////////////////////////////////////////////////////////////
									// MONITOR
		LONG64		_lUseSize;
		LONG64		_lAllocSize;

		//////////////////////////////////////////////////////////////////////////
		// OPTION
		bool		_bFreelist;		// 블록 동적 할당 여부(초기 AllocSize == 0 일 경우)
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

		// InterlockedCompareExchange128의 첫번째 인자는 시작 주소가 16바이트 정렬되어있어야 사용 가능
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

		// 꽉 참
		if (lAllocCount < InterlockedIncrement64(&_lUseSize))
		{
			if (!_bFreelist)
			{
				InterlockedDecrement64(&_lUseSize);
				return nullptr;
			}

			// 신규 할당
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