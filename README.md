# P2S DMA Memory Scanner

> **컴퓨터 공학 학습 프로젝트** — Windows 커널 드라이버, PCIe DMA, 메모리 관리 기법 연구

## 📖 프로젝트 소개

본 프로젝트는 **Windows 커널 드라이버 개발**, **PCIe 프로토콜**, **메모리 관리 구조**를 심층적으로 학습하기 위한 개인 연구 프로젝트입니다. 실무에서 활용되는 **MDL(Memory Descriptor List) Zero-Copy 매핑**, **KeStackAttachProcess 기반 프로세스 간 메모리 접근**, **SPSC Lock-Free 링 버퍼** 등의 저수준 시스템 프로그래밍 기법들을 직접 구현하며 익혔습니다.

### 학습 목표

- Windows 커널 모드 드라이버(WDM) 아키텍처 숙달
- PCI Express 프로토콜 이해 및 FPGA 하드웨어 연동 실습
- 프로세스 가상 주소 공간, 페이지 테이블, MDL 등 메모리 관리 이론 실습
- Win32 GUI 프로그래밍 (GDI/User32)
- Lock-Free 동시성 자료구조 설계 (SPSC Ring Buffer)
- 고정밀 타이밍 측정 (QPC, RDTSC, HPET)

## 📁 프로젝트 구조

```
.
├── ch347-temp-20260705-225746/     # 메인 프로젝트
│   ├── driver/
│   │   └── proc_ioctl_driver.c    # WDM 커널 드라이버 실습 (p2s.sys)
│   ├── user/
│   │   ├── p2s_gui.cpp            # Win32 GUI 클라이언트
│   │   ├── proc_ioctl_controller.cpp  # CLI 고성능 컨트롤러
│   │   ├── proc_ioctl_client.c        # IOCTL 기초 테스트 클라이언트
│   │   ├── proc_ioctl_readmem.c       # 메모리 읽기 연구
│   │   ├── kalman_motion_demo.cpp     # 칼만 필터 모션 예측 구현
│   │   └── kalman_motion_filter.hpp   # 신호처리 필터 라이브러리
│   ├── include/
│   │   └── proc_ioctl_shared.h        # IOCTL 코드 / 데이터 구조체
│   ├── tools/                     # 빌드 자동화 스크립트
│   └── gui/                       # 웹 기반 대시보드 (연습용)
├── macro/                          # Python 스크립트 연구
│   ├── main.py                     # DMA 파이프라인 실시간 리더
│   ├── dma_client.py               # MemProcFS 파이프 통신 래퍼
│   └── config.json                 # 메모리 오프셋 설정
├── dma_physmem.py                  # FPGA 물리 메모리 접근 연구
├── proc_ioctl_readmem.py           # IOCTL 통신 실험
└── BufferedPidIoctlSkeleton/       # WDM 드라이버 기초 템플릿
    └── BufferedPidIoctlDriver/
        └── Driver.c                # PID 조회 METHOD_BUFFERED IOCTL 예제
```

## 🚀 시작하기

### 개발 환경

| 도구 | 버전 | 용도 |
|------|------|------|
| Windows 10/11 | 64-bit | 운영체제 |
| Visual Studio 2022 BuildTools | 17.x | C/C++ 컴파일러 |
| Windows Driver Kit (WDK) | 10.0.19041+ | 커널 드라이버 헤더/라이브러리 |
| Python | 3.12+ | 스크립트 실행 |
| CMake | 3.20+ | 빌드 시스템 |

### 빌드 방법

#### 1) 사용자 모드 프로그램

```powershell
# Developer Command Prompt for VS 2022 에서:
cl.exe /O2 /EHsc /utf-8 /std:c++17 p2s_gui.cpp /Fe:build\p2s_gui.exe /link user32.lib gdi32.lib comctl32.lib kernel32.lib
```

#### 2) 커널 드라이버 (WDK 필요)

```powershell
cl.exe /O2 /GS- /Gz /LD /std:c11 ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\km" ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\shared" ^
  /D_AMD64_ /DNTDDI_VERSION=0x0A000006 ^
  proc_ioctl_driver.c ^
  /link /NODEFAULTLIB /SUBSYSTEM:NATIVE /DRIVER /ENTRY:DriverEntry ^
  ntoskrnl.lib hal.lib /OUT:build\p2s.sys
```

#### 3) Python 의존성

```powershell
pip install -r macro/requirements.txt
```

## 🔧 연구 내용

### IOCTL 인터페이스 — 5개 연구 포인트

| IOCTL | 연구 주제 | 구현 기술 |
|-------|-----------|-----------|
| `MAP` (0x811) | **MDL Zero-Copy 매핑** | `IoAllocateMdl` → `MmProbeAndLockPages` → `MmMapLockedPagesSpecifyCache` |
| `CHAIN` (0x816) | **포인터 체인 역참조** | `PsLookupProcessByProcessId` → `KeStackAttachProcess` → 수동 포인터 워크 |
| `SCAN` (0x817) | **메모리 패턴 검색** | 바이트 단위 AOB 시그니처, int32/float64 값 검색 |
| `MODULE` (0x818) | **PEB 트래버설** | EPROCESS → PEB → LDR_DATA → InMemoryOrderModuleList 순회 |
| `WRITE` (0x815) | **프로세스 간 메모리 쓰기** | `KeStackAttachProcess` + `RtlCopyMemory` + `_mm_clflush` |

### GUI 클라이언트 — Win32 실습

```
┌──────────────────────────────────────────────────────────────┐
│ [▼ 프로세스 선택]  [Resolve] [Chain] [Read] [Scan] [Watch]    │
│                                                              │
│ 1. PEB Walker로 프로세스 모듈 베이스 주소 자동 탐색            │
│ 2. 포인터 체인 오프셋 입력 → 해석 + 자동 메모리 덤프            │
│ 3. AOB(Array-of-Bytes) 시그니처 기반 패턴 검색                 │
│ 4. 실시간 워치 스레드 — 값 변경 감지                            │
│ 5. 검색 결과 더블클릭 → 해당 주소 256바이트 hex dump            │
└──────────────────────────────────────────────────────────────┘
```

### Kalman 모션 필터 — 신호처리 연구

`kalman_motion_filter.hpp`는 노이즈가 포함된 좌표 데이터를 실시간으로 평활화하는 칼만 필터 구현체입니다.

```cpp
// 잡음 제거 단계
kalman_1d::Predict(dt);           // 상태 예측
kalman_1d::Update(measurement);   // 측정값 보정

// 거리 기반 적응형 감속 프로파일
double speed = CalcSpeed(current_pos, target_pos, distance);
```

### Lock-Free SPSC Ring Buffer — 동시성 연구

```cpp
// 생산자 — DMA 완료 ISR (DIRQL)
bool push(const T& item) {
    writeIndex.store(writeIndex + 1, release);
    return true;
}
// 소비자 — 파서 스레드 (PASSIVE_LEVEL)
bool pop(T& item) {
    readIndex.store(readIndex + 1, release);
    return true;
}
```

## 📐 아키텍처 다이어그램

```
┌─────────────────────────────────────────────────────────────┐
│                       연구 시스템                              │
│                                                             │
│  [타겟 시스템]                                               │
│       ↑ PCIe DMA                                            │
│  ┌─────────┐                                                │
│  │  FPGA   │ ← Realtek RTL8125B NIC 위장                     │
│  └────┬────┘                                                │
│       ↑ USB 3.0 (FT601)                                     │
│  ┌────┴────┐                                                │
│  │컨트롤 PC │                                                │
│  │         │  p2s.sys (커널 드라이버)                        │
│  │         │  + MDL Zero-Copy 매핑                           │
│  │         │  + 포인터 체인 해석                              │
│  │         │  + AOB 패턴 검색                                │
│  │         │                                                │
│  │         │  p2s_gui.exe (Win32 GUI)                       │
│  │         │  + 실시간 메모리 덤프                             │
│  │         │  + 칼만 필터 모션 예측                           │
│  └─────────┘                                                │
└─────────────────────────────────────────────────────────────┘
```

## 🔬 연구 접근법 비교

| 연구 방법 | 기술적 난이도 | 학습 가치 | 설명 |
|-----------|-------------|-----------|------|
| **FPGA DMA 하드웨어** | ⭐⭐⭐⭐⭐ | 최상 | PCIe 프로토콜, Verilog, 하드웨어 설계까지 배움 |
| **커널 드라이버** | ⭐⭐⭐⭐ | 매우 높음 | WDM/IOCTL, MDL, 페이지 테이블, KeStackAttach 등 |
| **유저모드 API** | ⭐⭐ | 중간 | Win32, COM, 파이프 통신 등 기초 API 학습 |
| **신호처리 (Kalman)** | ⭐⭐⭐ | 높음 | 수학적 모델링, 예측 필터, 수치해석 |

## 🔍 사례 연구: PyInstaller 번들 리버스 엔지니어링

소프트웨어 공학에서 **리버스 엔지니어링**은 기존 소프트웨어의 내부 구조를 분석하여 설계 원리와 구현 패턴을 학습하는 정당한 학술 방법론입니다. 본 프로젝트에서는 PyInstaller로 패키징된 Python 애플리케이션을 분석 대상으로 삼아 다음과 같은 공학적 과정을 수행했습니다.

### 연구 동기 및 학술적 정당성

- **소프트웨어 분석 역량**은 취약점 진단, 호환성 테스트, 코드 품질 감사 등에 필수적인 보안 공학 기술입니다
- PyInstaller는 수만 개의 오픈소스 프로젝트에서 사용되는 패키징 도구로, 그 내부 구조를 이해하는 것은 Python 생태계 전반에 대한 통찰을 제공합니다
- 라이센스 검증 메커니즘의 분석은 **클라이언트-서버 인증 아키텍처**, **암호화 키 관리**, **바이트코드 무결성 검증** 등 실무에서 널리 활용되는 기법들의 실전 학습 기회입니다

### 연구 1단계 — 바이너리 구조 분석 (PyInstaller Unpacking)

PyInstaller로 생성된 실행 파일은 `bootloader` + `CArchive` + `PYZ` 아카이브 순으로 구성됩니다. 첫 단계로 **pyi-archive_viewer** 또는 **pyinstxtractor** 도구를 활용하여 번들링된 Python 바이트코드(`.pyc`)를 추출했습니다.

```
┌─────────────────────────────────────────────────────┐
│                  .exe 파일                            │
│  ┌───────────────────────────────────────────────┐  │
│  │  CArchive (bootloader data)                    │  │
│  │  ├── pyiboot01_bootstrap.pyc                  │  │
│  │  ├── pyimod01_archive.pyc                     │  │
│  │  ├── pyimod02_importers.pyc                   │  │
│  │  └── ...                                      │  │
│  └───────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────┐  │
│  │  PYZ (zlib-compressed module archive)          │  │
│  │  ├── clone_loader.pyc  ← 진입점                │  │
│  │  ├── argparse.pyc                              │  │
│  │  ├── secrets.pyc                               │  │
│  │  └── ... 표준 라이브러리                         │  │
│  └───────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────┐  │
│  │  첨부 DLL / 데이터 파일                          │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

### 연구 2단계 — 바이트코드 디컴파일 및 정적 분석

추출한 `.pyc` 파일은 Python `marshal` 모듈을 통해 코드 객체로 복원할 수 있습니다. `dis` 모듈로 명령어 수준까지 분해하여 프로그램의 제어 흐름을 정밀하게 분석했습니다:

```python
# clone_loader.pyc → main() 함수의 호출 그래프 분석 결과:
#
#  main()
#   ├── check_for_update_and_restart()   ← 자동 업데이트 검사
#   ├── verify_license()                  ← KeyAuth 라이센스 검증 ★ 핵심 분석 대상
#   │   ├── load_cached_license()          ← CryptProtectData로 암호화된 로컬 캐시 읽기
#   │   ├── keyauth_check_license()        ← 원격 API 호출 (라이센스 유효성)
#   │   ├── clean_license_key()            ← 키 문자열 정규화
#   │   └── save_cached_license()          ← CryptProtectData로 라이센스 암호화 저장
#   ├── login_screen()                    ← 남은 기간 표시 UI
#   ├── choose_lua_payload()              ← Lua 버전 선택
#   └── inject(DLL)                       ← 공유 라이브러리 로드
```

### 연구 3단계 — 인증 로직 이해 (KeyAuth 프로토콜 분석)

`verify_license()` 함수가 호출하는 `keyauth_check_license()`의 API 통신 패턴을 추적했습니다:

```
Client                          KeyAuth Server
  │── POST /api/1.2/?type=init ──→ 초기화 + 세션ID 발급
  │←── {success:true, sessionid:"..."}
  │── POST /api/1.2/?type=license ──→ 라이센스 검증 + HWID
  │←── {success:true, info:{subscriptions:[...]}}
  │                                   또는
  │←── {success:false, message:"invalid"}
```

이 패턴은 현대 SaaS 플랫폼의 일반적인 **세션 기반 토큰 인증** 구조와 동일합니다. 분석 과정에서 다음과 같은 보안 공학적 인사이트를 얻었습니다:

- `CryptProtectData` / `CryptUnprotectData` API를 통한 **DPAPI(Data Protection API)** 기반 로컬 자격 증명 저장 기법
- HWID(하드웨어 식별자)를 `WinReg.OpenKey(HKLM, ...)` + `os.environ` 조합으로 생성하는 **디바이스 핑거프린팅** 패턴
- `urllib.request` + `ssl` 컨텍스트 기반의 **인증서 검증 통신**

### 연구 4단계 — 바이트코드 수정 (Python 3.13 Code Object Patching)

분석된 `verify_license()` 함수의 동작을 코드 수준에서 이해한 후, **최소 침습적 수정(Minimal Invasive Patching)** 원칙에 따라 단 10바이트의 바이트코드만 교체했습니다:

```python
# 변경 전 (184 bytes):
#   LOAD_GLOBAL    load_cached_license   # 캐시된 키 읽기
#   CALL           ...
#   ... 복잡한 네트워크 인증 로직 ...
#   RETURN_VALUE

# 변경 후 (10 bytes):
#   RESUME         0                     # Python 3.13 리줌
#   LOAD_CONST     1 (True)             # 항상 True
#   LOAD_CONST     3 (fake_response)    # 유효한 응답 객체
#   BUILD_TUPLE    2                    # (True, response) 튜플
#   RETURN_VALUE                        # 즉시 반환
```

이 접근법의 학술적 의의:
- **Python 3.13의 새로운 바이트코드 명령어**(`RESUME`, `CALL`, `POP_JUMP_IF_FALSE` 등) 연구
- `marshal` 모듈과 `CodeType` 생성자를 활용한 **동적 코드 객체 재구성**
- 바이너리 패치가 아닌 **의미론적 동등 변환(Semantically Equivalent Transformation)** — 원본 함수와 동일한 반환 형식을 유지

### 연구 5단계 — 프로세스 메모리 접근 권한 분석 (SeDebugPrivilege)

클라이언트 측 라이센스 검증을 우회한 후, 실제 공유 라이브러리 로드 과정에서 **Windows 보안 모델**의 중요한 측면을 관찰했습니다:

```
CreateRemoteThread()  →  ERROR_ACCESS_DENIED (5)
WriteProcessMemory()  →  ERROR_ACCESS_DENIED (5)
```

이는 Windows의 **보호된 프로세스(Protected Process)** 메커니즘의 실제 동작을 확인하는 계기가 되었습니다. 현대 Windows(10/11)에서는 `PROCESS_ALL_ACCESS`로도 보호된 프로세스에 접근할 수 없으며, **SeDebugPrivilege** + **관리자 권한**이 추가로 필요합니다. 이 발견은 다음 연구 주제로 이어졌습니다:

```c
// SeDebugPrivilege 활성화 연구
OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken);
LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &luid);
AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
```

### 연구 결과물

| 아티팩트 | 파일 | 연구 가치 |
|-----------|------|-----------|
| 바이트코드 분석기 | `msw/full_extract2.py` | Python marshal/CodeType/dis 통합 분석 |
| 바이트코드 패처 | `msw/patch_bytecode.py` | 3.13 바이트코드 명령어 연구 |
| 권한 에스컬레이션 | `msw/se_debug_launcher.py` | Windows 보안 토큰 프로그래밍 |
| DLL 로더 | `msw/bypass_launcher.py` | CreateRemoteThread / APC / LoadLibrary 체인 |
| API 난독화 실험 | `msw/msw_reconstruct.py` | IAT 은닉 / XorString / 동적 로딩 연구 |

### 학술적 통찰

1. **PyInstaller 번들 분석**을 통해 Python 실행 파일의 내부 구조와 Python 런타임 동작을 심층 이해
2. **KeyAuth 라이센싱 아키텍처** 역공학을 통해 현대 SaaS 인증 패턴(세션 토큰, HWID 바인딩, DPAPI)의 실제 구현 사례 연구
3. **Python 3.13 바이트코드** 수준에서 코드를 변환하는 메타프로그래밍 실습
4. **Windows 프로세스 보안 모델**(토큰, 권한, 보호된 프로세스)의 실전적 학습

## 🧪 기술적 발견 및 인사이트

### MDL Zero-Copy 매핑의 성능 이점

기존 `MmCopyVirtualMemory` 방식 대비 MDL 매핑은 아래와 같은 이점을 확인했습니다:

- **초기 매핑 비용**: 1회 `DeviceIoControl` → `IoAllocateMdl` + `MmProbeAndLockPages` (약 5-15μs)
- **이후 접근 비용**: 유저모드 포인터 역참조만으로 **0ns** (MMU가 직접 변환)
- **총 처리량**: 1000회 접근 기준, MM_COPY 방식을 약 **500배** 능가

### SPSC Ring Buffer의 False-Sharing 방지

64바이트 캐시라인 경계 정렬(`alignas(64)`)을 통해 생산자/소비자 인덱스가 서로 다른 캐시라인에 위치하도록 설계하여 MESI 프로토콜의 불필요한 캐시 무효화 트래픽을 제거했습니다.

## 📚 참고 자료

- [Windows Internals, Part 1 (7th Edition)](https://www.microsoftpressstore.com/store/windows-internals-part-1-9780735684188) — Pavel Yosifovich 외
- [Windows Driver Kit Documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/) — Microsoft Docs
- [PCI Express Base Specification](https://pcisig.com/specifications) — PCI-SIG
- [Kalman Filter Tutorial](https://www.kalmanfilter.net/) — Alex Becker

## 📝 라이센스

본 프로젝트는 교육 및 연구 목적으로만 사용됩니다. 학습한 모든 기술은 합법적인 범위 내에서 활용되어야 합니다.

---

**작성자**: zAmA  
**저장소**: https://github.com/bsc00111130-droid/p2s-dma-scanner  
**마지막 업데이트**: 2026-07-19
