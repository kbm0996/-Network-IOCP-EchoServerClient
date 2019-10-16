/*---------------------------------------------------------------
  Lockfree Queue

  ������ �˰����� �̿��� ����ȭ ��ü���� ������ ������ ť
 �޸� Ǯ Ŭ���� - �����͸� �����ߴٰ� ���ִ� ����
 Ư�� ������(����ü,Ŭ����,����)�� ������ �Ҵ� �� ��������.

 - ����ȭ ������ ���
 ���� ȯ�濡 ���� ����ȭ �������� �� ���� ����.
 �ڵ忡 ��ȭ�� ���� �߻��ϹǷ� ����� �۵��� �� �׽�Ʈ�� �غ�����.
 �ٸ�, �����ϸ� volatile �������� �ذ��

 - ����
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
		// ������, �ı���.
		//
		// Parameters:	
		// Return:
		//////////////////////////////////////////////////////////////////////////
		CLFQueue();
		virtual ~CLFQueue();

		//////////////////////////////////////////////////////////////////////////
		// �ʱ�ȭ
		//
		// Parameters:	
		// Return:
		//////////////////////////////////////////////////////////////////////////
		void Clear();

		//////////////////////////////////////////////////////////////////////////
		// ť�� �����͸� �ִ´�.
		//
		// Parameters: (DATA) ������.
		// Return: 
		//////////////////////////////////////////////////////////////////////////
		void Enqueue(DATA Data);

		//////////////////////////////////////////////////////////////////////////
		// ������ �̱�
		//
		// Parameters: (DATA&) ��� ������.
		// Return: bool
		//////////////////////////////////////////////////////////////////////////
		bool Dequeue(DATA& Data);

		/////////////////////////////////////////////////////////////////////////
		// ������ �б�
		//
		// Parameters:DATA OutData, iPos ��ġ
		// Return: bool ���� ����
		/////////////////////////////////////////////////////////////////////////
		bool Peek(DATA& OutData, int iPeekPos = 0);
		bool isEmpty();

		//////////////////////////////////////////////////////////////////////////
		// ���� ������� �� ������ ��´�.
		//
		// Parameters: 
		// Return: (int) ������� �� ����
		//////////////////////////////////////////////////////////////////////////
		int GetUseSize() { return (int)_lUseSize; }
		int GetAllocSize() { return (int)_MemoryPool.GetAllocSize(); }

	private:
		// MEMORY POOL
		CLFMemoryPool<st_NODE> _MemoryPool;
		// NODE
		st_END_NODE*	_pHeadNode;
		st_END_NODE*	_pTailNode;
		LONG64			_iUnique;			// Top �ĺ�, ABA ���� ����
		// MONITOR
		LONG			_lUseSize;
	};

	template<class DATA>
	inline CLFQueue<DATA>::CLFQueue()
	{
		_iUnique = 0;
		_lUseSize = 0;

		/*---------------------------------------------------------------------------------------
		- Head, Tail�� Dummy�� �δ� ����
		Dummy�� ���� ������ NULL�� ���� �ƴ� ��� �� if ���� ���� ����ؾ���.
		Lockfree ������ ���������� InterlockedComparedExchanged()�� �ٽ��ε� ���� if���� ó���ϴ� ����
		��Ƽ������ ȯ�濡�� ���� ������ ���ؽ�Ʈ ����Ī���� ���� ������ ����� �� �ִ� ������ �ȴ�.
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
				// `Tail_Copy�� ���� ���`�� ������ NULL�̸�
				// `Tail_Copy�� ���� ���`�� Tail_New ����
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
				// `Tail_Copy�� ���� ���`�� ������
				// ���� `Tail`�� �ű�. Tail�� �ٲ������ iUnique���� �ٲ���
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
			// �������
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
					// Head�� Tail�� ����Ű�� ��尡 ���� ���
					st_END_NODE stTail_Copy;
					stTail_Copy.iUnique = _pTailNode->iUnique;
					stTail_Copy.pEndNode = _pTailNode->pEndNode;
					if (stTail_Copy.pEndNode->pNextNode != nullptr)
					{
						// `Tail_Copy�� ���� ���`�� ������ ���� `Tail` �ű��
						LONG64 liUnique = InterlockedIncrement64(&_iUnique);
						InterlockedCompareExchange128((LONG64*)_pTailNode, (LONG64)liUnique, (LONG64)stTail_Copy.pEndNode->pNextNode, (LONG64*)&stTail_Copy);
					}
				}
				else
				{
					// ���� ���� ���� ->  Head �̵�! ->  �޸����� ������.
					// Head���� �̵��ϰ� �޸𸮸� �����ϸ� ��带 �̵��ϰ� ��������, ������ �޸𸮸� ������ �� �ְų� ������������ �� ����.
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