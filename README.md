# SherlockEngine

DirectX 11 렌더링 엔진입니다. 최종 목표는 RHI(Render Hardware Interface)와 PSO(Pipeline State Object)이며, D3D11 백엔드를 먼저 만들고 D3D12로 추상화를 검증합니다.

렌더러 전체 구조와 설계 결정, 발전 로드맵은 [docs/rendering-analysis.html](docs/rendering-analysis.html)에 정리되어 있습니다.

## 빌드 준비

Visual Studio 2022(v143 도구 집합)와 Windows 10 SDK가 필요합니다. 의존성은 vcpkg 매니페스트(`vcpkg.json`)로 고정되어 있으므로 패키지를 따로 설치할 필요는 없습니다.

vcpkg를 처음 쓴다면 한 번만 준비합니다.

```bat
git clone https://github.com/microsoft/vcpkg
.\vcpkg\bootstrap-vcpkg.bat
.\vcpkg\vcpkg integrate install
```

이후 `SherlockEngine.sln`을 열고 빌드하면 `vcpkg.json`에 적힌 패키지가 `vcpkg_installed/`에 자동으로 설치됩니다. 첫 빌드는 패키지를 받거나 빌드하느라 몇 분 걸릴 수 있습니다.

명령줄에서 빌드하려면 다음과 같이 합니다.

```bat
msbuild SherlockEngine.sln /p:Configuration=Debug /p:Platform=x64 /m
```

지원 구성은 `x64`의 Debug와 Release입니다.

## 실행

셰이더는 빌드할 때가 아니라 실행할 때 `D3DCompileFromFile`로 컴파일합니다. `.hlsl` 파일을 상대 경로로 찾으므로 **작업 디렉터리를 `SherlockEngine\`으로 두고 실행해야 합니다.** Visual Studio에서 F5로 실행하면 기본 작업 디렉터리가 프로젝트 폴더라 그대로 동작합니다.

탐색기에서 `x64\Debug\SherlockEngine.exe`를 직접 실행하면 셰이더를 찾지 못해 다음처럼 실패하고 종료합니다.

```
[실패] 셰이더 컴파일 실패 : 파일 경로 : BasicVertexShader.hlsl 진입점 : VS_Main 타깃 : vs_5_0
[실패]   (컴파일러 메시지 없음. HRESULT = 0x80070002 (지정된 파일을 찾을 수 없습니다.). 셰이더 파일 경로를 확인할 것.)
```

실행 파일 기준 경로로 바꾸는 작업은 아직 하지 않았습니다.

## 소스 규약

- C++ 소스(`.h`, `.cpp`)는 **UTF-8 BOM**으로 저장합니다. 모든 구성에 `/utf-8`이 적용되어 있습니다.
- HLSL(`.hlsl`)은 **BOM 없이** 저장합니다. `D3DCompileFromFile`이 BOM을 `error X3000: Illegal character in shader file`로 거부합니다.
- 위 두 규칙은 `.editorconfig`에 들어 있습니다.
- D3D 인터페이스를 소유하면 `ComPtr`, 소유하지 않으면 생 포인터입니다.
- 엔진 API는 실패를 `bool`로 알립니다. 로그는 `Log::Info` / `Log::Warn` / `Log::Error`를 씁니다.

## 작업 로그

### 2026년 7월

- [2026/07/01 작업내용](logs/2026-07-01.md)
- [2026/07/02 작업내용](logs/2026-07-02.md)
- [2026/07/03 작업내용](logs/2026-07-03.md)
- [2026/07/04 작업내용](logs/2026-07-04.md)
- [2026/07/06 작업내용](logs/2026-07-06.md)
- [2026/07/07 작업내용](logs/2026-07-07.md)
- [2026/07/10 작업내용](logs/2026-07-10.md)
- [2026/07/16 작업내용](logs/2026-07-16.md)
