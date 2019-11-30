# 네트워크 프로그래밍 IOCP
## 📢 개요
 IOCP(입출력 완료 포트; I/O completion port) 에코 서버/클라이언트

## 💻 간

 다

  ![capture](주)
  
  **figure 1. 제*

## 📌 동작 원리

### 생성과 파괴

 IOCP는 비동기 입출력 결과와 이 결과를 처리할 스레드에 관한 정보를 담고 있는 구조
 
### 접근 제약

  Overlapped Callback 모델에선 APC Queue에 저장된 결과는 해당 APC Queue를 소유한 스레드만 확인할 수 있다.

 그러나 IOCP는 이런 제약이 없다. IOCP에 접근하는 스레드를 별도로 두는데, 이를 작업자 스레드(worker thread)라 부른다

### 비동기 입출력 처리 방법

  IOCP에 저장된 결과를 처리하려면 작업자 스레드는 GetQueuedCompletionStatus() 함수를 호출해야 한다. (이 때, 작업자 스레드가가 여러개 있어도 하나만 깨어난다)
 
  ![capture](주)
  
  **figure 2. 제*
 
① 임의의 스레드에서 비동기 입출력 함수를 호출하여 OS에 입출력 작업 요청.

② 모든 작업자 스레드는 GetQueuedCompletionStatus() 함수를 호출해 입출력 완료 포트를 감시. 완료한 비동기 입출력 작업이 아직 없다면 모든 작업자 스레드는 대기 상태. 대기 중인 작업자 스레드 목록은 입출력 완료 포트 내부에 저장.

③ 비동기 입출력 작업이 완료되면 OS는 입출력 완료 포트에 결과 저장. 이때 저장되는 정보를 입출력 완료 패킷(I/O completion packet)이라 부름.

④ OS는 입출력 완료 포트에 저장된 작업자 스레드 목록에서 하나를 선택하여 깨움. 대기 상태에서 깨어난 작업자 스레드는 비동기 입출력 결과를 처리. 작업자 스레드는 필요에 따라 다시 비동기 입출력 함수를 호출.


처음 시작할 때는 다음 그림과 같이 실행된다.

  ![capture](주)
  
  **figure 3. 제*


작업자 스레드에서 새로운 비동기 입출력을 시작하면 다음 그림과 같이 진행한다.

  ![capture](주)
  
  **figure 4. 제*
  
① CreateIoCompletionPort() 함수를 호출하여 입출력 완료 포트 생성

② CPU 개수에 비례하여 작업자 스레드 생성 (모든 작업자 스레드는 GetQueuedCompletionStatus() 함수를 호출하여 대기 상태)

③ 비동기 입출력을 지원하는 소켓 생성 (이 소켓에 대한 비동기 입출력 결과가 입출력 완료 포트에 저장되려면, CreateIoCompletionPort() 함수를 호출하여 소켓과 입출력 완료 포트 연결)

④ 비동기 입출력 함수 호출 (비동기 입출력 작업이 곧바로 완료되지 않으면, 소켓 함수는 SOCKET_ERROR를 리턴하고 오류 코드는 WSA_IO_PENDING으로 설정)

⑤ 비동기 입출력 작업이 완료되면, OS는 입출력 완료 포트에 결과를 저장하고 , 대기 중인 스레드 하나를 깨운다. (대기 상태에서 깨어난 작업자 스레드는 비동기 입출력 결과를 처리)

⑥ 새로운 소켓을 생성하면 3~5단계를, 그렇지 않으면 4~5단계를 반복



## 📌 입출력 완료 포트 생성 및 연결

 CreateCompletionPort() 함수는 두 가지 역할을 한다. 하나는 입출력 완료 포트를 새로 생성하는 일이고, 또 하나는 소켓과 입출력 완료 포트를 연결하는 일이다. 소켓과 입출력 완료 포트를 연결해두면 이 소켓에 대한 비동기 입출력 결과가 입출력 완료 포트에 저장된다.

```cpp
HANDLE CreateIoCompletionPort(
     HANDLE FileHandle,        // IOCP와 연결할 파일 핸들이나 소켓. INVALID_HANDLE_VALUE값 전달시 신규 생성
     HANDLE ExistingCompletionPort, // 연결할 IOCP 핸들, NULL이면 새 IOCP 생성
     ULONG_PTR CompletionKey,  // 입출력 완료 패킷(비동기 입출력 작업 완료시 생성되어 IOCP에 저장됨) 부가 정보
     DWORD NumberOfConcurrentThreads // 동시에 실행할 수 있는 스레드 수, 0 입력시 CPU 수만큼 스레드 수를 맞춤
); // 성공 : 입출력 완료 포트 핸들, 실패 : NULL
```
※ 참고로 _PTR로 끝나는 변수는 포인터 변수가 아니다. 포인터를 담을 수 있는 변수라는 의미

### 입출력 완료 포트를 새로 생성하는 코드 예제

```cpp
// Global 
HANDLE g_hIOCP; 

// Main
int main(int argc, char* argv[])
{
     g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0); 
     if(g_hIOCP == NULL) return 1;
   
     ...
   
     return 0;
}
```
※ 마지막 인자는 스레드를 몇개 생성하는지 설정하는 것인데 1을 넣는다고 스레드를 1개만 사용하는 것이 아니다. 이미 스레드가 Sleep() 등의 이유로 실행 중이면 1개 더 깨우기 때문이다. 따라서, 사용자가 원하는대로 항상 1개의 스레드만이 작동하는 것이 아니므로 0을 입력하여 CPU 개수만큼의 스레드를 사용하는 것이 보편적이다.

### 기존 소켓과 입출력 완료 포트를 연결하는 코드 예제

```cpp
// Global 
HANDLE g_hIOCP; 
SOCKET g_Sock;
// Main
int main(int argc, char* argv[])
{
     HANDLE hResult;
     g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0); 
     if(g_hIOCP == NULL) return 1;
   
     ...

     hResult = CreateIoCompletionPort((HANDLE)g_Sock, g_hIOCP, (DWORD)g_Sock, 0);
     if(hResult == NULL) return 1;

     ...
   
     return 0;
}
```

## 📌 비동기 입출력 결과 확인하기

 작업자 스레드는 GetQueuedCompletionStatus() 함수를 호출함으로써 입출력 완료 포트에 입출력 완료 패킷이 들어올 때까지 대기한다. 입출력 완료 패킷이 입출력 완료 포트에 들어오면 OS는 실행 중인 작업자 스레드의 개수를 체크한다. 이 값이 CreateIoCompletionPort() 함수의 네 번째 인자로 설정한 값보다 작다면, 대기 상태인 작업자 스레드를 깨워서 입출력 완료 패킷을 처리하게 한다.
 
 ```cpp
 BOOL GetQueuedCompletionStatus(
     HANDLE CompletionPort,           // IOCP 핸들
     LPDWORD lpNumberOfBytesTransferred,// __Out__ 비동기 입출력 작업으로 전송된 Byte수 
     PULONG_PTR lpCompletionKey,      // __Out__ CreateCompletionPort() 함수 호출 시 사용했던 3번째 인자
     LPOVERLAPPED *lpOverlapped,      // __Out__ 비동기 입출력 함수 호출 시 전달한 OVERLAPPED 구조체 주소
     DWORD dwMilliseconds             // 작업자 스레드가 대기할 시간 (ms)
);
 ```
 
 ### 작업자 스레드 예
 ```cpp
 while(1)
{
     ...

     DWORD Transferred = 0;
     Session* Key = 0;
     OVERLAPPED *pOver;

     result = GetQueuedCompletionStatus(g_HIOCP, &Transferred, &Key, &pOver, INFINITE); 

     ...

}
 ```
 함수 실패시 *lpOverlapped의 값이 NULL으로 변한다. 하지만, 실패시엔 Transferred, Key 인자의 값이 변하지 않기 때문에 재활용시 예전의 값을 갖고있을 수 있다. 따라서, 초기화가 필수적이다.

 응용 프로그램이 작업자 스레드에 특별한 사실을 알리기 위해 직접 입출력 완료 패킷을 생성할 수도 있다. 이때 사용하는 함수는 PostQueuedCompletionStatus()다.
