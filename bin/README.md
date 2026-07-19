# 바이너리 정적 분석 샘플

PE 포맷 연구 및 Win32 바이너리 분석 교육 자료.

## 파일

| 파일 | 분석 주제 |
|------|-----------|
| `msw_inject.exe` | PE 헤더, IAT(Import Address Table), `requireAdministrator` 매니페스트 |
| `apc_inject.exe` | `QueueUserAPC` API / `TH32CS_SNAPTHREAD` 사용 패턴 |
| `keep_inject.exe` | `WaitForSingleObject` 타임아웃 루프 구조 |
| `p2s_gui.exe` | Win32 GUI — GDI 오브젝트, Common Controls, `DeviceIoControl` |

## 분석 도구

```powershell
# PE 헤더 덤프
dumpbin /headers p2s_gui.exe

# IAT 분석
dumpbin /imports msw_inject.exe

# 섹션 레이아웃
dumpbin /sections apc_inject.exe

# 매니페스트 추출
mt.exe -inputresource:msw_inject.exe -out:manifest.xml

# GUI 리소스 구조 (pestudio, Resource Hacker)
```

## 참고

모든 바이너리는 본 저장소의 소스 코드에서 직접 컴파일된 것입니다.
