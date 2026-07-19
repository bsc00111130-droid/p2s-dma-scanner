# IPC 연구 — 프로세스 간 메모리 접근

Windows 프로세스 간 통신(IPC) 및 보안 토큰 실습.

## 파일

| 파일 | 연구 내용 |
|------|-----------|
| `msw_inject.cpp` | `CreateRemoteThread` + `VirtualAllocEx` + `WriteProcessMemory` |
| `apc_inject.cpp` | `QueueUserAPC` + `TH32CS_SNAPTHREAD` 스레드 열거 |
| `keep_inject.cpp` | 루프 대기 + `PROCESS_CREATE_THREAD` 최소 권한 실험 |

## 빌드

```powershell
cl.exe /O2 /EHsc /std:c++17 msw_inject.cpp /Fe:msw_inject.exe /link advapi32.lib
```

## 학습 포인트

- `OpenProcessToken` + `AdjustTokenPrivileges(SeDebugPrivilege)`
- `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` / `TH32CS_SNAPTHREAD`
- `PROCESS_ALL_ACCESS` vs `PROCESS_CREATE_THREAD` vs `PROCESS_VM_OPERATION` 권한 비교
- `PAGE_READWRITE` vs `PAGE_EXECUTE_READWRITE` 메모리 보호
- `WaitForSingleObject` 타이밍 패턴
