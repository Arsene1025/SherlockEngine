#pragma once
#include "Scene/Script.h"

// 게임 UI 검증용 (docs/plan/ui-canvas-plan.html 3~6단계). Button·Toggle·Slider 의 이벤트에 Inspector 로 연결하는 함수들
// (SHERLOCK_UI_ACTION). 부를 때마다 로그 한 줄 — 자동화 검증이 로그로 횟수·값을 셈 — 과 label 글자를 바꿈.
class UiClickCounter : public Script
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;
	void Start() override;

	void Increment();                        // 클릭 횟수 + 1
	void ResetCount();                       // 0 으로
	void ShowValue(float value);             // Slider·Scrollbar 값
	void ShowFlag(bool on);                  // Toggle
	void Say(const std::string& text);       // 고정 인자 문자열

	std::string label;                       // 바꿀 UIText 오브젝트 (상대 경로)
	std::string prefix = "Clicks";
	int count = 0;
	bool writeOnStart = true;                // 시작할 때 label 을 "prefix: count" 로 (끄면 씬에 적은 글자 그대로)

private:
	void SetLabel(const std::string& text);
};
