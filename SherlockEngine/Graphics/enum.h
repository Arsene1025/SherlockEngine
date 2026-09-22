#pragma once
#include <windows.h>   // UINT

// RS_* / RM_* 렌더 모드 열거형은 1단계에서 PSO(PipelineStateDesc)로 대체되어 삭제됐다.

enum class LightType : UINT
{
	Directional = 0,
	Point = 1,
	Spot = 2
};
constexpr UINT MAX_LIGHTS = 8;
