#include "pch.h"
#include "App/EditorHistory.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Core/Log.h"

namespace
{
	const std::string kEmptyLabel;
}

void EditorHistory::Reset(const Scene& scene)
{
	m_states.clear();
	m_states.push_back({ "(start)", SceneSerializer::SaveEditState(scene) });
	m_current = 0;
	m_bytes = m_states[0].text.size();
	m_gestureActive = false;
}

void EditorHistory::Rebase(const Scene& scene)
{
	if (m_states.empty()) { Reset(scene); return; }
	m_bytes -= m_states[m_current].text.size();
	m_states[m_current].text = SceneSerializer::SaveEditState(scene);
	m_bytes += m_states[m_current].text.size();
	m_gestureActive = false;
}

bool EditorHistory::Commit(const Scene& scene, const std::string& label)
{
	if (m_states.empty()) { Reset(scene); return false; }
	std::string text = SceneSerializer::SaveEditState(scene);
	if (text == m_states[m_current].text) return false;   // 바뀐 것이 없음 (선택만 한 클릭 등)

	// 되돌린 뒤 새 편집을 하면 그 뒤의 "다시 실행" 가지는 사라짐
	for (size_t i = m_current + 1; i < m_states.size(); ++i) m_bytes -= m_states[i].text.size();
	m_states.resize(m_current + 1);
	m_bytes += text.size();
	m_states.push_back({ label.empty() ? std::string("Edit") : label, std::move(text) });
	m_current = m_states.size() - 1;
	Trim();
	return true;
}

void EditorHistory::UpdateGesture(const Scene& scene, bool busy, const std::string& label)
{
	if (busy)
	{
		if (!m_gestureActive) { m_gestureActive = true; m_gestureLabel = label; }
		return;
	}
	if (!m_gestureActive) return;
	m_gestureActive = false;
	if (Commit(scene, m_gestureLabel)) Log::Info("되돌리기 이력: '%s' (단계 %zu, %.1f KB)", m_gestureLabel.c_str(), GetStepCount(), m_bytes / 1024.0);
}

void EditorHistory::Restore(Scene& scene)
{
	if (!SceneSerializer::RestoreEditState(scene, m_states[m_current].text))
	{
		Log::Error("되돌리기: 상태를 복원하지 못함 (단계 %zu)", m_current);
		return;
	}
	// 복원한 씬을 다시 직렬화해 기준으로 삼음: 복원이 표현을 조금이라도 바꾸면(예: 자식 순서) 다음 Commit 이 "바뀌었다" 고 오판하기 때문.
	m_bytes -= m_states[m_current].text.size();
	m_states[m_current].text = SceneSerializer::SaveEditState(scene);
	m_bytes += m_states[m_current].text.size();
	m_gestureActive = false;
}

bool EditorHistory::Undo(Scene& scene)
{
	if (!CanUndo()) return false;
	--m_current;
	Restore(scene);
	return true;
}

bool EditorHistory::Redo(Scene& scene)
{
	if (!CanRedo()) return false;
	++m_current;
	Restore(scene);
	return true;
}

const std::string& EditorHistory::GetUndoLabel() const
{
	return CanUndo() ? m_states[m_current].label : kEmptyLabel;
}

const std::string& EditorHistory::GetRedoLabel() const
{
	return CanRedo() ? m_states[m_current + 1].label : kEmptyLabel;
}

void EditorHistory::Trim()
{
	// 가장 오래된 상태부터 버림 (m_states[0] 은 "그보다 더 앞으로는 못 가는" 바닥이 되어 남음). 현재 위치는 항상 마지막이므로 앞에서만 줄어듦.
	while (m_states.size() > kMaxSteps + 1 || (m_bytes > kMaxBytes && m_states.size() > 2))
	{
		m_bytes -= m_states.front().text.size();
		m_states.erase(m_states.begin());
		if (m_current > 0) --m_current;
	}
}
