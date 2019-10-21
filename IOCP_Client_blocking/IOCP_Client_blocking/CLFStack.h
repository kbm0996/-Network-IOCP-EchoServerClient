/*---------------------------------------------------------------
  LockFree Stack

  Lockfree �˰����� �����Ͽ� Thread-Safety�� ����
 �޸� Ǯ Ŭ���� - �����͸� �����ߴٰ� ���ִ� ����
 Ư�� ������(����ü,Ŭ����,����)�� ������ �Ҵ� �� ��������.

- ����
CLFStack<UINT64>	_SessionStk;
UINT iCnt = 0;
_SessionStk.Push(iCnt);
UINT64 iIndex;
_SessionStk.Pop(iIndex);

- ����ȭ ������ ���
  ���� ȯ�濡 ���� ����ȭ �������� �� ���� ����.
 �ڵ忡 ��ȭ�� ���� �߻��ϹǷ� ����� �۵��� �� �׽�Ʈ�� �غ�����.
 �ٸ�, �����ϸ� volatile �������� �ذ��
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
		// ������, �ı���.
		//
		// Parameters:	
		// Return:
		//////////////////////////////////////////////////////////////////////////
		CLFStack();
		virtual	~CLFStack();

		//////////////////////////////////////////////////////////////////////////
		// ������ �ֱ�
		//
		// Parameters: (DATA) ������.
		// Return:
		//////////////////////////////////////////////////////////////////////////
		void Push(DATA Data);

		//////////////////////////////////////////////////////////////////////////
		// ������ �̱�
		//
		// Parameters: (DATA *) ��� ������
		// Return: (BOOL) TRUE, FALSE.
		//////////////////////////////////////////////////////////////////////////
		bool Pop(DATA& pOutData);

		//////////////////////////////////////////////////////////////////////////
		// �ʱ�ȭ
		//
		// Parameters:	
		// Return:
		//////////////////////////////////////////////////////////////////////////
		void Clear();

		bool isEmpty();

		//////////////////////////////////////////////////////////////////////////
		// ���� ���� �� ������ ����
		//
		// Parameters: ����.
		// Return: (int) ���� �� ������ ����.
		//////////////////////////////////////////////////////////////////////////
		int	GetUseSize() { return (int)_lUseSize; }
		int	GetAllocSize() { return (int)_MemoryPool.GetAllocSize(); }

	private:
		// MEMORY POOL
		CLFMemoryPool<st_NODE>	_MemoryPool;
		// NODE
		st_TOP_NODE*	_pTopNode;
		LONG64			_iUnique;			// Top �ĺ�, ABA ���� ����
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
			// �������
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