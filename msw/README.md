# MSWloader Reverse Engineering Case Study

> 학술 목적의 PyInstaller 번들 분석 및 바이트코드 연구

## 연구 개요

본 디렉토리는 PyInstaller로 패키징된 Python 애플리케이션의 내부 구조를 분석하는 **소프트웨어 역공학 실습** 결과물입니다. 연구 초점은 다음과 같습니다:

- PyInstaller CArchive/PYZ 아카이브 구조 분석
- Python 3.13 바이트코드 디컴파일 및 제어 흐름 분석
- KeyAuth 프로토콜 인증 아키텍처 연구
- Windows 프로세스 보안 모델 실습 (SeDebugPrivilege, Protected Process)

## 파일 설명

| 파일 | 연구 주제 |
|------|-----------|
| **patch_bytecode.py** | Python 3.13 `CodeType` 바이트코드 패칭 — marshal/dis/memmove 활용 |
| **patch_verify.py** | `verify_license()` 함수 디스어셈블 및 제어 흐름 그래프 |
| **full_extract.py / full_extract2.py** | PyInstaller PYZ 아카이브 추출 + `dis` 명령어 수준 분석 |
| **run_patched.py** | 패치된 코드 객체를 런타임에 로드하는 `importlib` 실험 |
| **bypass_launcher.py** | `ctypes.WinDLL` + `WriteProcessMemory` + `CreateRemoteThread` 인젝션 |
| **se_debug_launcher.py** | `AdjustTokenPrivileges(SeDebugPrivilege)` — Windows 보안 토큰 연구 |
| **msw_reconstruct.py** | 전체 애플리케이션 흐름 재구성 (APC/RemoteThread/Shellcode 경로) |
| **launcher.py** | `ctypes` 기반 DLL 인젝션 타이밍 프로파일링 |
| **check_arch.py** | `IsWow64Process` / `GetModuleHandle` / `GetProcAddress` 진단 |
| **runas.vbs** | `ShellExecute(runas)` 관리자 권한 상승 |

## 제외된 파일

바이너리 파일(`.dll`, `.exe`, `.lua`)은 Git 저장소에 포함되지 않습니다.
이는 오픈소스 배포 가이드라인을 준수하기 위함입니다.

## 참고

본 연구는 소프트웨어 공학 교육 및 보안 분석 방법론 학습을 목적으로 수행되었습니다.
