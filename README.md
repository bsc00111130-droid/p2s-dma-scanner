# P2S — Windows Kernel Driver & Process Memory Analysis

> **컴퓨터 공학 연구 프로젝트**  
> WDM 커널 드라이버, 프로세스 가상 메모리, Lock-Free 자료구조, 정적 바이너리 분석

## 프로젝트 개요

본 저장소는 **Windows 커널 드라이버 개발**, **프로세스 메모리 관리 구조**, **Lock-Free 동시성 알고리즘**을 학습하기 위한 개인 연구 프로젝트입니다. 직접 WDM 드라이버를 작성하고 `IoCreateDevice` → `METHOD_BUFFERED` / `METHOD_NEITHER` IOCTL 디스패치 구조를 실습했습니다.

모든 테스트는 **개발자가 직접 작성한 더미 프로세스**를 대상으로 수행되었으며, 실제 타사 소프트웨어를 대상으로 한 실험은 포함되어 있지 않습니다.

## 디렉토리 구조

```
.
├── ch347-temp-20260705-225746/
│   ├── driver/proc_ioctl_driver.c    # WDM 커널 드라이버 (p2s.sys)
│   ├── user/
│   │   ├── p2s_gui.cpp               # Win32 GUI — 메모리 덤프 뷰어
│   │   ├── proc_ioctl_controller.cpp # CLI — IOCTL 타이밍 측정
│   │   ├── proc_ioctl_client.c       # IOCTL 기본 예제
│   │   ├── proc_ioctl_readmem.c      # 메모리 읽기 연습
│   │   ├── kalman_motion_demo.cpp    # 칼만 필터 수치 예제
│   │   └── kalman_motion_filter.hpp  # 범용 신호처리 라이브러리
│   ├── include/proc_ioctl_shared.h   # 커널 ↔ 유저 공용 구조체
│   ├── tools/                        # 빌드/PowerShell 자동화
│   └── gui/                          # 웹 대시보드 (Node.js 연습)
├── macro/                            # Python 연구 노트
│   ├── main.py                       # DMA 파이프라인 프로토타입
│   └── dma_client.py                 # 파이프 통신 래퍼
├── msw/                              # PyInstaller 바이트코드 분석 연구 (크랙미)
│   ├── patch_bytecode.py             # marshal/CodeType 패치 실험
│   ├── full_extract2.py              # dis 바이트코드 디스어셈블러
│   └── README.md                     # 연구 설명
├── bin/                              # 정적 분석용 바이너리 샘플
│   ├── p2s_gui.exe                   # 자체 빌드 GUI
│   ├── msw_inject.exe                # 인젝션 테스트 바이너리
│   └── README.md                     # dumpbin/pyinstxtractor 가이드
├── dma_physmem.py                    # FPGA 메모리 접근 프로토타입
├── proc_ioctl_readmem.py             # IOCTL 통신 연습
└── BufferedPidIoctlSkeleton/          # WDM 기초 템플릿
```

## 연구 주제

### 1. WDM 커널 드라이버 — IOCTL 통신

| IOCTL | 연구 내용 |
|-------|-----------|
| `0x811` | `IoAllocateMdl` + `MmProbeAndLockPages` + `MmMapLockedPagesSpecifyCache` — MDL 매핑 |
| `0x816` | `PsLookupProcessByProcessId` + `KeStackAttachProcess` — 프로세스 간 VA 읽기 |
| `0x817` | AOB(Array-of-Bytes) 시그니처 검색 알고리즘 |
| `0x818` | `PsGetProcessPeb` → `PEB_LDR_DATA` → `InMemoryOrderModuleList` — PEB 순회 |
| `0x815` | 프로세스 간 메모리 쓰기 + `_mm_clflush` 캐시 무효화 |

### 2. Lock-Free 자료구조

```cpp
// SPSC Ring Buffer — 캐시라인 분리로 False-Sharing 방지
template<typename T, size_t Cap>
class SpscRingBuffer {
    std::array<T, Cap> entries_;       // 64B 정렬 슬롯
    alignas(64) std::atomic<size_t> writeIndex_;  // 생산자 전용 캐시라인
    alignas(64) std::atomic<size_t> readIndex_;   // 소비자 전용 캐시라인
};
```

### 3. Kalman 필터 — 신호처리

잡음이 포함된 좌표 데이터를 실시간 평활화하는 칼만 필터 구현 (`kalman_motion_filter.hpp`).  
드론/로봇 위치 추정, 센서 데이터 노이즈 제거 등의 응용을 위한 범용 라이브러리입니다.

```cpp
kalman.UpdateMeasurement(z);   // 측정값 보정
double p = kalman.GetPosition();
double v = kalman.GetVelocity();
```

### 4. PyInstaller 바이트코드 분석 (크랙미 실습)

`msw/` 디렉토리에는 자체 제작한 PyInstaller 번들 바이너리를 `marshal` + `dis`로 디컴파일하고,
`CodeType` 생성자를 통해 바이트코드를 수정하는 **크랙미(crackme)** 스타일의 리버스 엔지니어링
연습이 포함되어 있습니다.

```python
# Python 3.13 바이트코드 패치 예제 (patch_bytecode.py)
new_code = types.CodeType(
    co_argcount, co_nlocals, 2, co_flags,
    bytes([0x97, 0x00, 0x64, 0x01, 0x53, 0x00]),  # RESUME; LOAD_CONST 1; RETURN_VALUE
    ...)
```

## 빌드 및 실행

```powershell
# GUI 빌드 (VS 2022 BuildTools)
cl.exe /O2 /EHsc /utf-8 /std:c++17 p2s_gui.cpp /Fe:build\p2s_gui.exe ^
  /link user32.lib gdi32.lib comctl32.lib kernel32.lib

# 드라이버 빌드 (WDK 필요)
cl.exe /O2 /GS- /Gz /LD /std:c11 /D_AMD64_ /I"C:\...\km" ^
  proc_ioctl_driver.c /link /NODEFAULTLIB /SUBSYSTEM:NATIVE /DRIVER ^
  /ENTRY:DriverEntry ntoskrnl.lib /OUT:build\p2s.sys
```

## 참고 문헌

- [Windows Internals, Part 1 (7th Ed.)](https://www.microsoftpressstore.com/store/windows-internals-part-1-9780735684188) — Yosifovich, Ionescu, Russinovich, Solomon
- [Windows Driver Kit Documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/) — Microsoft Docs
- [Lock-Free Programming](https://www.1024cores.net/home/lock-free-algorithms) — Dmitry Vyukov
- [An Introduction to the Kalman Filter](https://www.cs.unc.edu/~welch/kalman/) — Welch & Bishop

## 라이센스

본 저장소는 개인 학습 및 학술 연구 목적으로 공개됩니다.  
모든 코드는 이해관계 없는 더미 프로세스를 대상으로 테스트되었습니다.

---

**작성자**: zAmA  
**저장소**: https://github.com/bsc00111130-droid/p2s-dma-scanner
