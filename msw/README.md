# 크랙미 실습: PyInstaller 바이트코드 분석

> Python 3.13 바이트코드 리버스 엔지니어링 교육 자료

## 목적

이 디렉토리는 **자체 제작한** PyInstaller 번들 바이너리를 분석 대상으로 하는 **크랙미(crackme)** 스타일의 교육 자료입니다. `dis` 모듈로 바이트코드 명령어를 분석하고, `marshal`/`CodeType`으로 코드 객체를 수정하는 실습을 제공합니다.

## 학습 주제

- PyInstaller CArchive / PYZ 아카이브 구조 이해
- Python 3.13 바이트코드 명령어 세트 (`RESUME`, `CALL`, `POP_JUMP_IF_FALSE` 등)
- `marshal.load()` / `types.CodeType()` 을 통한 동적 코드 생성
- `ctypes.WinDLL` + 함수 시그니처 명시적 바인딩

## 파일 설명

| 파일 | 학습 주제 |
|------|-----------|
| `patch_bytecode.py` | Python 3.13 CodeType 생성자로 바이트코드 패치 |
| `patch_verify.py` | `dis` 모듈로 함수 제어 흐름 그래프 출력 |
| `full_extract2.py` | PyInstaller PYZ 아카이브에서 모듈 추출 + 일괄 분석 |
| `run_patched.py` | 패치된 모듈을 `sys.path`로 우선 로드하는 `importlib` 응용 |
| `se_debug_launcher.py` | `AdjustTokenPrivileges` — Windows 보안 토큰 API 실습 |
| `bypass_launcher.py` | `CreateRemoteThread` + `VirtualAllocEx` — IPC 연구 |
| `msw_reconstruct.py` | `ctypes.WinDLL` 바인딩 + `struct` 언패킹 연습 |

## 연습 방법

```python
# 1. Python 바이트코드 추출
import marshal
with open("clone_loader.pyc", "rb") as f:
    f.read(16)  # PyInstaller 헤더
    code = marshal.load(f)

# 2. 디스어셈블
import dis
for const in code.co_consts:
    if hasattr(const, "co_code"):
        dis.dis(const)

# 3. 패치
new_code = types.CodeType(...)
```

## 참고

이 자료는 자신이 직접 작성한 Python 프로그램을 PyInstaller로 패키징하고
이를 스스로 분석하는 **자가 학습용 크랙미** 형태로 설계되었습니다.
