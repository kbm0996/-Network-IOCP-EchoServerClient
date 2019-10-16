/*---------------------------------------------------------------
  Lockfree Queue

  락프리 알고리즘을 이용한 동기화 객체없이 스레드 안전한 큐
 메모리 풀 클래스 - 데이터를 보관했다가 빼주는 역할
 특정 데이터(구조체,클래스,변수)를 일정량 할당 후 나눠쓴다.

 - 최적화 컴파일 대비
 개발 환경에 따라서 최적화 컴파일을 할 수도 있음.
 코드에 변화가 많이 발생하므로 제대로 작동할 지 테스트를 해봐야함.
 다만, 웬만하면 volatile 선언으로 해결됨

 - 사용법
CLFQueue<UINT64> _SessionQ;
UINT iCnt = 0;
_SessionQ.Enqueue(iCnt);
UINT64 iIndex;
_SessionQ.Dequeue(iIndex);
----------------------------------------------------------------*/
#ifndef  __LF_QUEUE__
#define  __LF_QUEUE__
#include <Windows.h>
#include "CLFMemoryPool.h"

namespace mylib
{
	template <class DATA>
	class CLFQueue
	{
	private:
		struct st_NODE
		{
			DATA Data;
			st_NODE* pNextNode;
		};
		struct st_END_NODE
		{
			st_NODE	*pEndNode;
			__int64	iUnique;
		};

	public:
		//////////////////////////////////////////////////////////////////////////
		// 생성자, 파괴자.
		//
		// Parameters:	
		// Return:
		//////////////////////////////////////////////////////////////////////////
		CLFQueue();
		virtual ~CLFQueue();

		//////////////////////////////////////////////////////////////////////////
		// 초기화
		//
		// Parameters:	
		// Return:
		//////////////////////////////////////////////////////////////////////////
		void Clear();

		//////////////////////////////////////////////////////////////////////////
		// 큐에 데이터를 넣는다.
		//
		// Parameters: (DATA) 데이터.
		// Return: 
		//////////////////////////////////////////////////////////////////////////
		void Enqueue(DATA Data);

		//////////////////////////////////////////////////////////////////////////
		// 데이터 뽑기
		//
		// Parameters: (DATA&) 출력 데이터.
		// Return: bool
		//////////////////////////////////////////////////////////////////////////
		bool Dequeue(DATA& Data);

		/////////////////////////////////////////////////////////////////////////
		// 데이터 읽기
		//
		// Parameters:DATA OutData, iPos 위치
		// Return: bool 성공 여부
		/////////////////////////////////////////////////////////////////////////
		bool Peek(DATA& OutData, int iPeekPos = 0);
		bool isEmpty();

		//////////////////////////////////////////////////////////////////////////
		// 현재 사용중인 블럭 개수를 얻는다.
		//
		// Parameters: 
		// Return: (int) 사용중인 블럭 개수
		//////////////////////////////////////////////////////////////////////////
		int GetUseSize() { return (int)_lUseSize; }
		int GetAllocSize() { return (int)_MemoryPool.GetAllocSize(); }

	private:
		// MEMORY POOL
		CLFMemoryPool<st_NODE> _MemoryPool;
		// NODE
		st_END_NODE*	_pHeadNode;
		st_END_NODE*	_pTailNode;
		LONG64			_iUnique;			// Top 식별, ABA 문제 방지
		// MONITOR
		LONG			_lUseSize;
	};

	template<class DATA>
	inline CLFQueue<DATA>::CLFQueue()
	{
		_iUnique = 0;
		_lUseSize = 0;

		/*---------------------------------------------------------------------------------------
		- Head, Tail을 Dummy로 두는 이유
		Dummy를 두지 않으면 NULL일 경우와 아닐 경우 등 if 문을 많이 사용해야함.
		Lockfree 구조의 로직에서는 InterlockedComparedExchanged()가 핵심인데 많은 if문을 처리하는 것은
		멀티스레드 환경에서 성능 문제나 컨텍스트 스위칭으로 인한 문제가 잦아질 수 있는 요인이 된다.
		----------------------------------------------------------------------------------------*/
		_pHeadNode = (st_END_NODE*)_aligned_malloc(sizeof(st_END_NODE), 16);
		_pHeadNode->pEndNode = _MemoryPool.Alloc();
		_pHeadNode->pEndNode->pNextNode = nullptr;
		_pHeadNode->iUnique = 0;

		_pTailNode = (st_END_NODE*)_aligned_malloc(sizeof(st_END_NODE), 16);
		_pTailNode->pEndNode = _pHeadNode->pEndNode;
		_pTailNode->iUnique = _pHeadNode->iUnique;
	}

	template<class DATA>
	inline CLFQueue<DATA>::~CLFQueue()
	{
		Clear();

		_aligned_free(_pHeadNode);
		_aligned_free(_pTailNode);
	}

	template<class DATA>
	inline void CLFQueue<DATA>::Clear()
	{
		if (_lUseSize > 0)
		{
			st_END_NODE *pTop = _pHeadNode;
			st_NODE *pNode;
			while (pTop->pEndNode->pNextNode != nullptr)
			{
				pNode = pTop->pEndNode;
				pTop->pEndNode = pTop->pEndNode->pNextNode;
				_MemoryPool.Free(pNode);
				_lUseSize--;
			}

		}
	}

	template<class DATA>
	inline void CLFQueue<DATA>::Enqueue(DATA Data)
	{
		st_NODE *pNode_New = _MemoryPool.Alloc();
		pNode_New->Data = Data;
		pNode_New->pNextNode = nullptr;

		volatile st_END_NODE stTail_New;
		stTail_New.pEndNode = pNode_New;
		stTail_New.iUnique = InterlockedIncrement64(&_iUnique);

		while (1)
		{
			volatile st_END_NODE stTail_Copy;
			stTail_Copy.iUnique = _pTailNode->iUnique;
			stTail_Copy.pEndNode = _pTailNode->pEndNode;

			if (stTail_Copy.pEndNode->pNextNode == nullptr)
			{
				// `Tail_Copy의 다음 노드`가 여전히 NULL이면
				// `Tail_Copy의 다음 노드`에 Tail_New 삽입
				if (InterlockedCompareExchangePointer(
					(PVOID*)&stTail_Copy.pEndNode->pNextNode,
					stTail_New.pEndNode,
					nullptr
				) == nullptr)
				{
					if (_pTailNode->iUnique == stTail_Copy.iUnique)
						InterlockedCompareExchange128(
						(LONG64*)_pTailNode,
							(LONG64)stTail_New.iUnique, (LONG64)stTail_New.pEndNode,
							(LONG64*)&stTail_Copy
						);
					break;
				}
			}
			else
			{
				// `Tail_Copy의 다음 노드`가 있으면
				// 실제 `Tail`를 옮김. Tail이 바뀌었으니 iUnique값도 바꿔줌
				LONG64 liUnique = InterlockedIncrement64(&_iUnique);
				InterlockedCompareExchange128(
					(LONG64*)_pTailNode,
					(LONG64)liUnique, (LONG64)stTail_Copy.pEndNode->pNextNode,
					(LONG64*)&stTail_Copy
				);
			}
		}

		InterlockedIncrement(&_lUseSize);
	}

	template<class DATA>
	inline bool CLFQueue<DATA>::Dequeue(DATA & Data)
	{
		if (InterlockedDecrement(&_lUseSize) < 0)
		{
			// 비어있음
			InterlockedIncrement(&_lUseSize);
			return false;
		}

		volatile st_END_NODE stHead_New;
		stHead_New.iUnique = InterlockedIncrement64(&_iUnique);

		while (1)
		{
			volatile st_END_NODE stHead_Copy;
			stHead_Copy.iUnique = _pHeadNode->iUnique;
			stHead_Copy.pEndNode = _pHeadNode->pEndNode;

			stHead_New.pEndNode = stHead_Copy.pEndNode->pNextNode;

			if (stHead_New.pEndNode != nullptr)
			{
				if (stHead_Copy.pEndNode == _pTailNode->pEndNode)
				{
					// Head와 Tail이 가리키는 노드가 같은 경우
					st_END_NODE stTail_Copy;
					stTail_Copy.iUnique = _pTailNode->iUnique;
					stTail_Copy.pEndNode = _pTailNode->pEndNode;
					if (stTail_Copy.pEndNode->pNextNode != nullptr)
					{
						// `Tail_Copy의 다음 노드`가 있으면 실제 `Tail` 옮기기
						LONG64 liUnique = InterlockedIncrement64(&_iUnique);
						InterlockedCompareExchange128((LONG64*)_pTailNode, (LONG64)liUnique, (LONG64)stTail_Copy.pEndNode->pNextNode, (LONG64*)&stTail_Copy);
					}
				}
				else
				{
					// 먼저 값을 복사 ->  Head 이동! ->  메모리해제 순으로.
					// Head부터 이동하고 메모리를 해제하면 헤드를 이동하고 들어왔으나, 엉뚱한 메모리를 해제할 수 있거나 존재하지않을 수 있음.
					Data = stHead_New.pEndNode->Data;
					if (InterlockedCompareExchange128((LONG64*)_pHeadNode, (LONG64)stHead_New.iUnique, (LONG64)stHead_New.pEndNode, (LONG64*)&stHead_Copy))
					{
						_MemoryPool.Free(stHead_Copy.pEndNode);
						break;
					}
				}
			}
		}
		return true;
	}

	template<class DATA>
	inline bool CLFQueue<DATA>::Peek(DATA & OutData, int iPeekPos)
	{
		if (_lUseSize <= 0 || _lUseSize < iPeekPos)
			return false;

		st_NODE *pNode = _pHeadNode->pEndNode->pNextNode;
		int iPos = 0;
		while (1)
		{
			if (pNode == nullptr)
				return false;

			if (iPos == iPeekPos)
			{
				OutData = pNode->Data;
				return true;
			}
			pNode = pNode->pNextNode;
			++iPos;
		}
	}

	template<class DATA>
	inline bool CLFQueue<DATA>::isEmpty()
	{
		if (_lUseSize == 0)
		{
			if ((_pHeadNode->pEndNode) == (_pTailNode->pEndNode))
				return true;
		}

		return false;
	}
}

#endif