# CMake 빌드와 IDE 분석 설정

이 프로젝트의 실제 빌드는 CMake + Visual Studio 2022/MSVC다. `ycnet` 타깃은 `cxx_std_23`을 요구하며 생성된 MSVC 프로젝트는 `/std:c++latest`를 사용한다. C++20의 concepts/requires/ranges도 이 모드에서 인식한다.

## 빨간 줄의 원인

2026-10-15 확인한 IDE의 언어 서버는 clangd 18.1.8이었다. clangd 로그에 다음 내용이 있었다.

```text
Failed to find compilation database
Generic fallback command
... -std=c++14 ...
'ycnet/rudp.hpp' file not found
```

즉 실제 빌드와 달리 언어 서버에 표준 옵션과 프로젝트 include 경로가 전달되지 않았다. 실행 중인 해당 clangd의 부모 프로세스는 Antigravity IDE였다. IDE 화면 자체의 빨간 줄은 직접 확인하지 않았다.

Visual Studio CMake 생성기는 `compile_commands.json`을 내보내지 않는다. `CMAKE_EXPORT_COMPILE_COMMANDS`는 [Ninja/Makefile 생성기에서 지원](https://cmake.org/cmake/help/latest/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html)한다. clangd의 [프로젝트 설정](https://clangd.llvm.org/config)은 이 파일을 참조할 수 있다.

## 적용한 구성

추가로 자동 include 정렬 때문에 `MSWsock.h`가 `WinSock2.h`보다 먼저 오면 RIO 선언에 필요한 `LONG`, `ULONG` 등의 정의를 찾지 못한다. 이는 `type_traits`의 `is_void_v<std::thread>`와 `std::function` 등에서 연쇄 진단을 만들 수 있다. 2026-10-15 사용자가 보고한 오류를 이 순서에서 재현했다. `rio_transport.hpp`의 Winsock→RIO 확장 헤더 순서를 복구했고, `.clang-format`의 `SortIncludes: Never`로 포맷할 때 순서가 바뀌지 않게 했다. 이 문제는 언어 표준 설정 누락과 별도의 원인이었다.

- 실제 빌드: 기존 `build`, Visual Studio/MSVC, 출력 `build/Release` 유지.
- IDE 분석: `build_analysis`, CMake/Ninja로 실제 타깃의 명령을 추출. 여기서는 빌드나 테스트를 실행하지 않는다.
- `.clangd`: 분석 데이터베이스를 지정. 헤더 단독 분석에서 표준 옵션이 제거되는 clangd 동작도 고려해 `.hpp`에는 `/TP`, `/std:c++latest`를 추가.
- `CMakePresets.json`: CMake를 지원하는 IDE가 `windows-msvc` 구성과 Release/Debug 빌드 구성을 선택할 수 있도록 제공.
- MSVC가 허용하던 `sizeof 타입명` 표현 세 곳을 표준적인 `sizeof(타입명)`으로 변경하고, 패킷/결과 헤더가 직접 사용하는 표준 헤더를 추가.

## 새 환경 또는 CMake 타깃 변경 후

```powershell
pwsh -NoProfile -File tools/configure_editor.ps1
```

VS Installer의 `vswhere`로 C++ 도구와 Ninja를 찾고, CMake로 `build_analysis/compile_commands.json`을 생성한다. 이 디렉터리는 Git에 포함하지 않는다. 분석 구성에서는 이미 실제 VS 빌드로 확인하는 MSVC의 링크 검사를 생략한다(`CMAKE_CXX_COMPILER_WORKS=TRUE`). 이것은 분석 DB 생성 용도이며 정상 빌드 설정에는 적용하지 않는다.

IDE에서는 현재 `ycnet` 폴더를 열어야 한다. 설정을 다시 읽지 않는 경우 언어 서버를 재시작하거나 IDE 창을 새로고침한다. Visual Studio에서는 `CMakeLists.txt` 또는 `CMakePresets.json`이 있는 폴더를 열고 `windows-msvc` 구성을 선택한다.

## 실제 빌드

```powershell
cmake --preset windows-msvc
cmake --build --preset release --parallel
```

기존 `cmake --build build --config Release` 방식도 동일하게 사용할 수 있다. 네트워크 테스트의 방화벽 허용 경로를 유지하기 위해 출력 디렉터리는 변경하지 않는다.

clangd `--check`의 `tweak: ... FAIL`은 체크 모드에서 실행하는 리팩터링 기능 검사 실패일 수 있다. 소스의 파서 진단과 구별해야 한다. 실제 IDE의 남은 빨간 줄은 해당 파일·줄·진단 문구로 확인한다.
