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
