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

솔루션은 세 프로젝트입니다 (11-D단계):

| 프로젝트 | 산출물 | 내용 |
|---|---|---|
| `SherlockEngine` | `SherlockEngine.lib` (정적) | 엔진 전부: Core · Graphics · RHI · Scene · Game(컴포넌트·스크립트) · Shaders · Assets 복사 |
| `SherlockEditor` | `SherlockEditor.exe` | 에디터 (TestApp · Editor · ContentBrowser · ScriptCreator · GameBuilder). **시작 프로젝트로 두고 F5** |
| `SherlockGame` | `SherlockGame.exe` | 게임 런타임 (GameApp): 에디터 없이 `engine.ini` 의 `[game] startScene` 을 로드해 바로 재생. 에디터의 File > Build Game... 이 `Build\<이름>\` 에 패키징 |

셰이더는 빌드할 때가 아니라 실행할 때 `D3DCompileFromFile`로 컴파일합니다. 빌드가 `SherlockEngine\Shaders\*.hlsl`을 `x64\<구성>\Shaders\`로 복사하고, 실행 파일은 `Core/Paths.h`로 **자기 위치 기준**의 그 폴더를 찾습니다. 작업 디렉터리는 상관없으므로 탐색기에서 `x64\Debug\SherlockEditor.exe`를 직접 실행해도 됩니다. (문서의 `SherlockEngine.exe` 는 11-D 이전 이름으로, 지금의 `SherlockEditor.exe` 입니다.)

exe 옆 `Shaders\` 폴더가 없으면(셰이더만 고치고 빌드하지 않은 경우) 소스 트리의 `SherlockEngine\Shaders\`로 폴백하고 `[경고]` 로그를 한 줄 남깁니다.

## 소스 폴더

| 폴더 | 내용 |
|---|---|
| `App/` | 창·메시지 루프·ImGui 프레임(`AppBase`), 데모 앱(`TestApp`) |
| `Core/` | 로그, 타이머, 경로 등 그래픽스와 무관한 유틸리티 |
| `Graphics/` | 렌더러, 메시, API 중립 타입 |
| `Graphics/D3D11/` | **`d3d11.h`를 include하는 파일은 이 폴더에만 둡니다.** |
| `Scene/` | 카메라, 트랜스폼, 게임 오브젝트, 컴포넌트 기반(`Behaviour`), 씬 직렬화 |
| `Game/Components/` | 엔진 컴포넌트 (Rigidbody, FollowTarget). `SHERLOCK_BEHAVIOUR` 로 등록. 엔진 lib 에 있어 모든 프로젝트에서 쓰인다 |
| `../Projects/<이름>/` | **프로젝트** (11-E, 언리얼 .uproject): `<이름>.sherlock` + `Assets\` + `Scripts\`(유니티 MonoBehaviour / `.h`+`.cpp` 쌍, `SHERLOCK_SCRIPT`). 에디터의 File > Projects 로 만들면 `<이름>.sln`(에디터·게임 타깃)이 생성된다. `Projects\Sample` 이 예제이고 엔진 솔루션의 SherlockEditor/SherlockGame 이 그 에디터·게임이다 (docs/phase-11e.html) |
| `Shaders/` | HLSL |

`#include`는 프로젝트 루트 기준으로 씁니다. 예: `#include "Graphics/D3D11/Device.h"`.

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
