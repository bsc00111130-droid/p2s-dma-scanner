# P2S DMA Memory Scanner

FPGA PCIe DMA 기반 프로세스 메모리 스캐너 / 인젝터 도구 모음

## 프로젝트 구조

```
├── ch347-temp-20260705-225746/     # 메인 프로젝트
│   ├── driver/proc_ioctl_driver.c  # WDM 커널 드라이버 (p2s.sys)
│   ├── user/p2s_gui.cpp            # Win32 GUI 메모리 스캐너
│   ├── user/proc_ioctl_controller.cpp  # CLI 컨트롤러 (IOCTL → 시리얼)
│   ├── user/proc_ioctl_client.c        # IOCTL 테스트 클라이언트
│   ├── user/proc_ioctl_readmem.c       # 메모리 읽기 클라이언트
│   ├── user/kalman_motion_demo.cpp     # 칼만 필터 모션 데모
│   ├── user/kalman_motion_filter.hpp   # 칼만 필터 헤더
│   ├── include/proc_ioctl_shared.h     # 공용 IOCTL / 구조체 정의
│   ├── tools/                          # 빌드 / 검증 스크립트
│   └── gui/                            # 웹 GUI
├── macro/                          # Python DMA 클라이언트
│   ├── main.py                     # 실시간 메모리 리더
│   ├── dma_client.py               # MemProcFS 파이프 통신
│   ├── config.json                 # 오프셋 설정
│   └── requirements.txt
├── dma_physmem.py                  # FPGA 물리 메모리 DMA 리더
├── proc_ioctl_readmem.py           # IOCTL 기반 메모리 읽기
├── BufferedPidIoctlSkeleton/       # WDM 드라이버 기초 스켈레톤
│   └── BufferedPidIoctlDriver/
│       ├── Driver.c                # PID 조회 기초 드라이버
│       ├── dma_physmem.c           # FPGA DMA 물리 메모리 확장
│       └── Public.h
└── msw/                            # MSWloader 라이센스 바이패스
    ├── bypass_launcher.py          # 인증 우회 런처
    ├── se_debug_launcher.py        # SeDebugPrivilege 인젝터
    └── msw_reconstruct.py          # 원본 MSWloader 재구성
```

## 시작하기

### 사전 요구사항

- **Windows 10/11 x64**
- **Visual Studio 2022 BuildTools** (C++ 컴파일러)
- **WDK 10.0.19041+** (Windows Driver Kit, 드라이버 빌드용)
- **Python 3.12+** (스크립트용)

### 빌드

#### 드라이버 (p2s.sys)

```powershell
# Developer Command Prompt for VS 2022 에서:
cd driver
cl.exe /O2 /GS- /Gz /LD /std:c11 /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\km" /D_AMD64_ /DNTDDI_VERSION=0x0A000006 proc_ioctl_driver.c /link /NODEFAULTLIB /SUBSYSTEM:NATIVE /DRIVER /ENTRY:DriverEntry ntoskrnl.lib hal.lib /OUT:..\build\p2s.sys
```

#### GUI (p2s_gui.exe)

```powershell
cl.exe /O2 /EHsc /utf-8 /std:c++17 p2s_gui.cpp /Fe:..\build\p2s_gui.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib kernel32.lib
```

#### 전체 빌드

```powershell
# 또는 build_all.bat 실행
.\build_all.bat
```

### 설치

```powershell
# 1. 테스트 서명 모드 활성화 (관리자 권한 필요)
bcdedit /set testsigning ON
# 재부팅

# 2. 드라이버 설치 및 시작
sc.exe create P2S type= kernel binPath= "C:\...\build\p2s.sys"
sc.exe start P2S

# 3. GUI 실행
.\build\p2s_gui.exe
```

## 기능

### 커널 드라이버 (p2s.sys) — 5개 IOCTL

| IOCTL | 코드 | 설명 |
|-------|------|------|
| `MAP` | 0x820 | MDL zero-copy 매핑 — 타겟 프로세스 메모리를 유저 VA에 직접 매핑 |
| `UNMAP` | 0x821 | 매핑 해제 |
| `CHAIN` | 0x822 | 멀티레벨 포인터 체인 자동 해석 (KeStackAttachProcess) |
| `SCAN` | 0x823 | AOB 패턴 / Int32 / Float / Int64 값 검색 |
| `MODULE` | 0x824 | PEB walker로 모듈 베이스 주소 탐색 |

### GUI (p2s_gui.exe) — Win32 데스크톱 앱

```
┌─────────────────────────────────────────────────────────────┐
│ [▼ 프로세스 선택     ] [Resolve] [Chain] [Read] [Scan] [Watch] │
│ Base: [0x0_____] Offsets: [0x1A0 0x2B8]                      │
│ AOB: [48 8B ? ? ? ?____] [Scan] [Skip: 0x0]                  │
├───────────────────┬───────────────────┬─────────────────────┤
│ Chain Results      │ Scan Results      │ Hex Dump            │
│                    │                   │                     │
├───────────────────┴───────────────────┴─────────────────────┤
│ Log Console                                                   │
└─────────────────────────────────────────────────────────────┘
```

### CLI (proc_ioctl_controller.exe)

```powershell
# 메모리 읽기 + 시리얼 전송 (고성능)
.\proc_ioctl_controller.exe <pid> <hex_addr> <size> COM3 [period_us]

# 예: 1000μs 간격으로 0x7FF600001234에서 64바이트 읽기 → COM3 전송
.\proc_ioctl_controller.exe 1234 0x7FF600001234 64 COM3 1000

# Dry-run (시리얼 없이 테스트)
.\proc_ioctl_controller.exe 1234 0x7FF600001234 64 - 1000
```

### Python DMA 클라이언트

```powershell
pip install -r macro/requirements.txt
python macro/main.py
```

### MSWloader 라이센스 바이패스

```powershell
# 관리자 권한 CMD에서:
cd msw
python bypass_launcher.py
```

## 데이터 흐름

```
[게임 PC] ← PCIe DMA ← [FPGA (Realtek 위장)]
                           ↑ USB (FT601)
[컨트롤 PC]
  ├─ FTDI WinUSB 드라이버 (signed)
  ├─ leechcore.dll + vmm.dll (MemProcFS)
  ├─ macro/main.py (Python 리더)
  └─ p2s_gui.exe (독립 실행형)
```

## 접근 방식 비교

| 방식 | 특징 | 장점 | 단점 |
|------|------|------|------|
| **DMA (FPGA)** | PCIe로 물리 메모리 직접 접근 | 완전 은닉, 게임PC 무부하 | 하드웨어 필요 |
| **Kernel Driver** | p2s.sys MDL zero-copy | 게임PC에서 커널 권한으로 작동 | testsigning 필요 |
| **User-mode Inject** | CreateRemoteThread/APC | 단순, 설정 불필요 | 현대 안티치트에 차단됨 |
| **MSWloader Bypass** | 라이센스 우회 + DLL 인젝션 | 기존 도구 활용 | 인젝션 실패 가능성 |

## 구축 과정

1. ✅ **WDM IOCTL 스켈레톤** — PCIe BAR 매핑, VA→PA 변환, DMA read/write IOCTL
2. ✅ **MDL Zero-Copy** — `IoAllocateMdl` → `MmProbeAndLockPages` → `MmMapLockedPagesSpecifyCache`
3. ✅ **포인터 체인 워커** — `KeStackAttachProcess`로 다중 레벨 포인터 역참조
4. ✅ **AOB 패턴 스캐너** — Mask/Value 기반 시그니처 검색
5. ✅ **PEB 모듈 리졸버** — PEB → LDR_DATA → InMemoryOrderModuleList walk
6. ✅ **SPSC Lock-Free 링 버퍼** — 생산자(DMA ISR) / 소비자(파서) 간 무잠금
7. ✅ **Win32 GUI** — 프로세스 선택, Resolve, Chain, Read, Scan, Watch
8. ✅ **고정밀 QPC 타이밍** — QueryPerformanceCounter 기반 μs 단위 스케줄링
9. ✅ **Async OVERLAPPED 시리얼** — 8슬롯 병렬 WriteFile
10. ✅ **MSWloader 라이센스 바이패스** — clone_loader.pyc verify_license 함수 패치
11. ✅ **SeDebugPrivilege 인젝터** — APC/RemoteThread 듀얼 방식
12. ✅ **Kalman 모션 필터** — 노이즈 좌표 평활화, 거리 기반 가속 제한

## 라이센스

교육 및 시스템 호환성 테스트 목적으로 제작되었습니다.

## 작성자

zAmA
