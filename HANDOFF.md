# DMA Remote Memory Reader & Injector — Handoff

## 프로젝트 개요

FPGA PCIe DMA 하드웨어(Realtek RTL8125B NIC 위장)를 통해 타겟 PC의 물리 메모리를 읽고 쓰는 도구입니다. FTP(FT601 USB 3.0) 인터페이스로 원격 PC에서 FPGA를 제어합니다.

## 아키텍처

```
[게임PC] ←── PCIe DMA ──→ [FPGA] ←── FTDI USB ──→ [원격PC]
 (타겟)                    (NIC 위장)               (이 프로그램 실행)
                                                    
게임PC에는 아무것도 설치 불필요. 모든 작업은 원격PC에서.
```

## 현재 작업 상태

### ✅ 완료된 것
- MSWloader 라이센스 우회 (verify_license 바이트코드 패치)
- 7가지 DLL 인젝션 방법 시도 (전부 안티치트에 차단됨)
- DMA reader 스크립트 작성 (vmm.dll + leechcore.dll 사용)
- FTDI 드라이버 설치/인식 확인
- 원격PC에서 DMA 연결 테스트 완료

### ❌ 안 되는 것  
- 유저모드 DLL 인젝션 (게임 안티치트 CIG로 전부 차단)
- DMA에서 DLL 실행 (바이트 쓰기까지는 되나 스레드 생성/APC 차단)

### 🔄 진행 중
- DMA 메모리 직접 읽기 (FTDI 인식됨, vmm.dll 연결 필요)
- DMA 메모리 직접 쓰기 (VMMDLL_MemWrite 확인됨)
- DLL 바이트를 DMA로 타겟 메모리에 쓰고 실행시키는 방법 연구

## 필요 파일 (원격PC)

```
C:\Users\zaku\Desktop\RemoteDMA\
├── FTD3XX.dll           (514KB)  FTDI USB3 드라이버 인터페이스
├── leechcore.dll        (139KB)  PCILeech DMA 코어
├── vmm.dll              (2.1MB)  MemProcFS 메모리 접근 API
├── vcruntime140.dll     (86KB)   VC++ 런타임
├── dbghelp.dll          (1.8MB)  디버그 도우미
├── symsrv.dll           (257KB)  심볼 서버
├── lone-dma-test.exe    (2.4MB)  DMA 연결 테스트 도구
├── dma_read.py          (8KB)    DMA 메모리 읽기
├── dma_inject.py        (5KB)    DMA DLL 쓰기
├── setup.bat            (1KB)    초기 설정
├── runas.vbs            (103B)   관리자 실행
└── mswloader_extracted\          (MSWloader 추출 파일들)
    ├── clone_loader.pyc          (라이센스 패치 완료)
    ├── clone_loader_patched.pyc  (verify_license -> return True)
    ├── run_loader.py             (라이센스 우회 런처)
    ├── MSWorld.dll       (9.1MB)  주요 게임 로직
    ├── planet_inject.dll (160KB)  DLL 인젝터
    ├── net_hook.dll      (112KB)  네트워크 훅
    ├── output_filter.dll (109KB)  출력 필터
    ├── PM_runtime_v2.lua (779KB)  Lua v2 페이로드
    └── run_patched.py    (1KB)    패치된 로더 실행
```

### 추가 게임PC 파일 (C:\msw\)
```
C:\msw\
├── p2s_gui.exe          (187KB)  Win32 GUI 메모리 스캐너
├── msw_inject.exe       (143KB)  CreateRemoteThread 인젝터
├── apc_inject.exe       (143KB)  QueueUserAPC 인젝터
├── keep_inject.exe      (143KB)  루프 인젝터
├── section_inject.py    (10KB)   NtCreateSection 인젝터
├── duphandle_inject.py  (5KB)    DuplicateHandle 우회
├── fast_inject.py       (5KB)    ACCESS_INJECT 타이밍 인젝터
└── msw_launch.bat       (1KB)    관리자 자동 실행
```

## 주요 API (vmm.dll / MemProcFS)

```python
import ctypes
from ctypes import wintypes

VMM = ctypes.WinDLL("vmm.dll")

# 연결
VMM.VMMDLL_Initialize(0, "")  # -> True/False

# PID 찾기  
VMM.VMMDLL_PidGetFromName("msw.exe")  # -> DWORD

# 메모리 읽기
VMM.VMMDLL_MemRead(pid, addr, out_buf, size)  # -> True/False

# 메모리 쓰기
VMM.VMMDLL_MemWrite(pid, addr, data_buf, size)  # -> True/False

# 모듈 베이스 주소
VMM.VMMDLL_ProcessGetModuleBase(pid, "module.dll")  # -> uint64

# 종료
VMM.VMMDLL_Close()
```

## 알려진 이슈

1. **VMMDLL_MemAlloc 없음** — 할당은 VMMDLL_MemWrite로 빈 영역에 직접 써야 함 
2. **DLL 실행 불가** — 바이트는 썼지만 LoadLibraryW/CreateRemoteThread/APC 전부 CIG 차단
3. **testsigning 필요** — p2s.sys 커널 드라이버는 testsigning ON 필요 (게임 실행 안 될 수 있음)
4. **FTDI 케이블** — FPGA에서 원격PC로 USB 케이블 물리적 연결 필수

## 다음 스텝 (해야 할 일)

### 우선순위 1: DMA 메모리 읽기 완성
```cmd
cd C:\Users\zaku\Desktop\RemoteDMA
python dma_read.py --proc msw.exe --addr 0x140000000 --size 64
```
이게 동작하면 게임 메모리를 읽을 수 있습니다.

### 우선순위 2: DMA로 DLL 실행
DLL 바이트를 타겟 프로세스에 쓴 후 실행시키는 방법:
- **옵션 A**: DLL을 프로세스에 쓰고, LDR(Loader) 리스트에 수동 추가
- **옵션 B**: 기존 스레드의 RIP를 shellcode로 변경 (SetThreadContext 대신 DMA로 TrapFrame 수정)
- **옵션 C**: MSWorld.dll을 disk에 쓰고 게임이 로드하게 유도

### 우선순위 3: p2s.sys 커널 드라이버 (testsigning ON 필요)
```
bcdedit /set testsigning ON
재부팅
sc start P2S
p2s_gui.exe
```

## 참고 링크

- GitHub: https://github.com/bsc00111130-droid/p2s-dma-scanner
- 모든 소스코드, 빌드 스크립트, 문서 포함
- MSWloader Patcher: msw/patch_bytecode.py (Python 3.13 bytecode 수정)
- Injector: inject/ 디렉토리 (C++ CreateRemoteThread/APC/SetThreadContext)

---

**마지막 업데이트**: 2026-07-19
**작업 계정**: zAmA (게임PC), zaku (원격PC)
