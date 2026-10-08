#pragma once

// 2026-10-08: 엔진 DLL 의 내보내기 표시 (스크립트 DLL, B안).
//
// 엔진은 SherlockEngine.dll 이고, 프로젝트 스크립트는 <프로젝트>Scripts.dll 로 따로 빌드되어 에디터·게임이 LoadLibrary 로 올림.
// 스크립트 DLL 이 엔진의 함수·클래스를 부르려면 엔진 DLL 이 그 심볼을 내보내야 함:
//   - 엔진 프로젝트(SherlockEngine.vcxproj)는 SHERLOCK_ENGINE_EXPORTS 를 정의해 __declspec(dllexport)
//   - 그 밖(에디터·게임 exe, 스크립트 DLL)은 __declspec(dllimport)
// 내보내는 범위는 "스크립트가 쓰는 API" 뿐임: Behaviour·Script·GameObject·Transform·Scene·Camera·CameraComponent·Input·Time·Rigidbody,
// Log·Paths·BehaviourRegistry 의 함수, 그리고 exe 진입점(App/EntryPoints.h). 렌더러·RHI·에디터 내부는 내보내지 않음.
//
// 정적 라이브러리를 DLL 로 바꾼 이유: 컴포넌트 레지스트리·로그·입력 같은 전역 상태가 프로세스에 한 벌만 있어야 함.
// 스크립트 DLL 이 엔진 lib 를 따로 링크하면 그 DLL 안에 전역 상태의 복사본이 생겨, 스크립트가 등록한 컴포넌트를 에디터가 보지 못함.
#ifdef SHERLOCK_ENGINE_EXPORTS
#define SHERLOCK_API __declspec(dllexport)
#else
#define SHERLOCK_API __declspec(dllimport)
#endif

// C4251: 내보내는 클래스의 std::string·std::vector 멤버에 "dll-interface 가 없다" 경고. 엔진과 스크립트 DLL 은 같은 컴파일러·같은 CRT(/MD)로
// 빌드하는 것이 전제이므로(프로젝트 생성기가 같은 설정을 씀) 표준 라이브러리 타입을 경계 너머로 넘겨도 안전함.
// C4275: 내보내는 클래스가 내보내지 않는 기반 클래스(PropertyVisitor 같은 순수 인터페이스)를 상속할 때의 같은 종류 경고.
#pragma warning(disable : 4251 4275)
