#pragma once
#include <cstddef>
#include <string>
#include <vector>

class Scene;

// 2026-10-15: 에디터의 되돌리기 이력 (Ctrl+Z / Ctrl+Y).
//
// 방식: "편집 상태 스냅샷의 사슬". 편집 한 번 = 그 직후의 상태 문자열(SceneSerializer::SaveEditState) 하나가 이력에 쌓이고, 되돌리기는
// 한 칸 앞의 문자열로 씬을 복원함(RestoreEditState). 명령 객체(Do/Undo 쌍)를 위젯마다 만드는 대신 "씬 전체의 편집 가능한 부분"을 통째로 기록하므로:
//   + 위젯 114 개와 기즈모·드롭·컴포넌트 필드 어느 것도 고치지 않고 모든 편집이 되돌려짐 (새 위젯이 생겨도 자동)
//   + 삭제의 되돌리기가 자식·컴포넌트·안정 ID·오브젝트 순서까지 그대로 돌아옴
//   − 편집 한 번이 씬 크기만큼의 문자열을 남김 (단계 수와 총 바이트로 상한을 둠)
//   − 메시·이미지는 상태에 없음: 모델 배치를 되돌려도 메시는 씬에 남음 (오브젝트만 사라짐)
//
// "언제 한 단계로 기록하는가"는 두 경로임.
//   1) 명령(오브젝트 추가·삭제·모델 배치·복제 …): EditorApp::RunCommand 가 실행 앞뒤로 Commit 을 부름.
//   2) 위젯 편집(슬라이더·기즈모·색·체크박스·계층 끌기 …): UpdateGesture 가 매 프레임 "입력이 진행 중인가" 를 받아, 진행 중이었다가
//      끝난 프레임에 Commit 함. 슬라이더를 끄는 동안은 아무것도 기록하지 않으므로 끌기 한 번 = 한 단계임.
// Commit 은 현재 상태가 기준(마지막 단계)과 같으면 아무것도 하지 않으므로, 씬을 바꾸지 않은 클릭(선택)은 단계를 만들지 않음.
//
// 재생 중에는 기록하지 않음 (Stop 이 재생 전 씬을 되돌리므로). 씬을 통째로 갈아 끼우는 경로(새 씬·불러오기·프로젝트 전환)는 Reset 으로
// 이력을 비움 — 메시·재질 목록이 바뀌어 인덱스가 맞지 않게 되기 때문.
class EditorHistory
{
public:
	static constexpr size_t kMaxSteps = 100;                  // 되돌릴 수 있는 단계 수
	static constexpr size_t kMaxBytes = 96u * 1024u * 1024u;  // 상태 문자열 총합 상한. 넘으면 가장 오래된 것부터 버림

	// 이력을 비우고 지금 씬을 첫 상태로 삼음.
	void Reset(const Scene& scene);
	// 이력은 두고 기준 상태만 지금 씬으로 바꿈 (재생 정지·스크립트 다시 올리기처럼 씬이 "같은 내용으로 다시 만들어진" 뒤).
	void Rebase(const Scene& scene);
	// 지금 씬이 기준과 다르면 label 이라는 한 단계로 기록함 (그 뒤쪽의 다시 실행 가능한 단계는 버림). 기록했으면 true.
	bool Commit(const Scene& scene, const std::string& label);
	// 매 프레임 UI 를 그린 뒤 부름. busy = 위젯이 활성이거나 마우스 버튼이 눌려 있거나 기즈모를 잡고 있음.
	// busy 가 true → false 로 바뀐 프레임에 Commit 함. label 은 입력이 시작될 때(첫 busy 프레임)의 것을 씀.
	void UpdateGesture(const Scene& scene, bool busy, const std::string& label);

	// 복원. 호출 전에 Commit 으로 진행 중이던 편집을 기록해 둘 것 (그래야 그 편집도 되돌릴 수 있음).
	// 씬의 오브젝트 포인터가 모두 바뀌므로 호출자는 선택을 ID 로 다시 잡아야 함.
	bool Undo(Scene& scene);
	bool Redo(Scene& scene);

	bool CanUndo() const { return m_current > 0; }
	bool CanRedo() const { return m_current + 1 < m_states.size(); }
	const std::string& GetUndoLabel() const;   // 되돌리면 취소되는 단계의 이름
	const std::string& GetRedoLabel() const;
	size_t GetStepCount() const { return m_states.empty() ? 0 : m_states.size() - 1; }
	size_t GetCurrentIndex() const { return m_current; }
	size_t GetBytes() const { return m_bytes; }

private:
	struct State
	{
		std::string label;   // 이 상태를 만든 편집의 이름. 첫 상태는 "(start)"
		std::string text;    // SceneSerializer::SaveEditState
	};
	void Restore(Scene& scene);   // m_states[m_current] 를 씬에 적용하고, 다시 직렬화해 기준 문자열을 실제 씬과 맞춤
	void Trim();

	std::vector<State> m_states;
	size_t m_current = 0;     // 지금 씬이 속한 상태. 그 뒤(m_current+1…)가 다시 실행할 단계
	size_t m_bytes = 0;
	bool m_gestureActive = false;
	std::string m_gestureLabel;
};
