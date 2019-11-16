/*---------------------------------------------------------------
  LockFree Stack

  Lockfree 알고리즘을 적용하여 Thread-Safety한 스택
 메모리 풀 클래스 - 데이터를 보관했다가 빼주는 역할
 특정 데이터(구조체,클래스,변수)를 일정량 할당 후 나눠쓴다.

- 사용법
CLFStack<UINT64>	_stkSession;
UINT iCnt = 0;
_stkSession.Push(iCnt);
UINT64 iIndex;
_stkSession.Pop(iIndex);

- 최적화 컴파일 대비
  개발 환경에 따라서 최적화 컴파일을 할 수도 있음.
 코드에 변화가 많이 발생하므로 제대로 작동할 지 테스트를 해봐야함.
 다만, 웬만하면 volatile 선언으로 해결됨
----------------------------------------------------------------*/
#ifndef  __LF_STACK__
#define  __LF_STACK__
#include <Windows.h>
#include "CLFMemoryPool.h"

namespace mylib
{
	//typedef int DATA;
	template <class DATA>
	class CLFStack
	{
	private:
		struct st_NODE
		{
			DATA Data;
			st_NODE	*pNextNode;
		};
		struct st_TOP_NODE
		{
			st_NODE	*pTopNode;
			__int64	iUnique;
		};

	public:
		//////////////////////////////////////////////////////////////////////////
		// 생성자, 파괴자.
		//
		// Parameters:	
		// Return:
		//////////////////////////////////////////////////////////////////////////
		CLFStack();
		virtual	~CLFStack();

		//////////////////////////////////////////////////////////////////////////
		// 데이터 넣기
		//
		// Parameters: (DATA) 데이터.
		// Return:
		//////////////////////////////////////////////////////////////////////////
		void Push(DATA Data);

		//////////////////////////////////////////////////////////////////////////
		// 데이터 뽑기
		//
		// Parameters: (DATA *) 출력 데이터
		// Return: (BOOL) TRUE, FALSE.
		//////////////////////////////////////////////////////////////////////////
		bool Pop(DATA& pOutData);

		//////////////////////////////////////////////////////////////////////////
		// 초기화
		//
		// Parameters:	
		// Return:
		//////////////////////////////////////////////////////////////////////////
		void Clear();

		bool isEmpty();

		//////////////////////////////////////////////////////////////////////////
		// 현재 스택 내 데이터 개수
		//
		// Parameters: 없음.
		// Return: (int) 스택 내 데이터 개수.
		//////////////////////////////////////////////////////////////////////////
		int	GetUseSize() { return (int)_lUseSize; }
		int	GetAllocSize() { return (int)_MemoryPool.GetAllocSize(); }

	private:
		// MEMORY POOL
		CLFMemoryPool<st_NODE>	_MemoryPool;
		// NODE
		st_TOP_NODE*	_pTopNode;
		LONG64			_iUnique;			// Top 식별, ABA 문제 방지
		// MONITOR
		LONG			_lUseSize;
	};

	template<class DATA>
	inline CLFStack<DATA>::CLFStack()
	{
		_iUnique = 0;
		_lUseSize = 0;

		_pTopNode = (st_TOP_NODE*)_aligned_malloc(sizeof(st_TOP_NODE), 16);
		_pTopNode->pTopNode = nullptr;
		_pTopNode->iUnique = _iUnique;
	}

	template<class DATA>
	inline CLFStack<DATA>::~CLFStack()
	{
		Clear();
		_aligned_free(_pTopNode);
	}

	template<class DATA>
	inline void CLFStack<DATA>::Push(DATA Data)
	{
		volatile st_NODE* pNode_New = _MemoryPool.Alloc();
		pNode_New->Data = Data;
		pNode_New->pNextNode = nullptr;

		volatile __int64 iUnique = InterlockedIncrement64(&_iUnique);

		volatile st_TOP_NODE stTopNode_Copy;
		do
		{
			stTopNode_Copy.iUnique = _pTopNode->iUnique;
			stTopNode_Copy.pTopNode = _pTopNode->pTopNode;

			pNode_New->pNextNode = _pTopNode->pTopNode;
		} while (!InterlockedCompareExchange128(
			(LONG64*)_pTopNode,
			iUnique, (LONG64)pNode_New,
			(LONG64*)&stTopNode_Copy));

		InterlockedIncrement(&_lUseSize);
	}

	template<class DATA>
	inline bool CLFStack<DATA>::Pop(DATA & pOutData)
	{
		if (InterlockedDecrement(&_lUseSize) < 0)
		{
			// 비어있음
			InterlockedIncrement(&_lUseSize);
			return false;
		}

		volatile st_TOP_NODE stTopNode_Copy;
		volatile st_NODE* pNode_Next;
		volatile __int64 iUnique = InterlockedIncrement64(&_iUnique);

		do {
			stTopNode_Copy.iUnique = _pTopNode->iUnique;
			stTopNode_Copy.pTopNode = _pTopNode->pTopNode;

			pNode_Next = stTopNode_Copy.pTopNode->pNextNode;
			pOutData = stTopNode_Copy.pTopNode->Data;
		} while (!InterlockedCompareExchange128(
			(LONG64*)_pTopNode,
			iUnique, (LONG64)pNode_Next,
			(LONG64*)&stTopNode_Copy));

		_MemoryPool.Free(stTopNode_Copy.pTopNode);
		return true;
	}

	template<class DATA>
	inline void CLFStack<DATA>::Clear()
	{
		if (_lUseSize > 0)
		{
			DATA Data;
			while (true)
			{
				if (Pop(Data))
					break;
			}
		}
	}

	template<class DATA>
	inline bool CLFStack<DATA>::isEmpty()
	{
		if (_lUseSize == 0)
		{
			if (_pTopNode->pTopNode == nullptr)
				return true;
		}

		return false;
	}
}
#endif