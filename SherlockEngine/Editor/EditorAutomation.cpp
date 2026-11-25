#include "pch.h"
#include "Editor/EditorApp.h"
#include "Editor/DebugUI.h"
#include "Editor/ScriptCreator.h"
#include "Editor/GameBuilder.h"
#include "Editor/ProjectLauncher.h"
#include "Scene/CameraComponent.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "Core/Input.h"
#include "UI/UIComponents.h"
#include "UI/UISystem.h"
#include <cmath>
#include <format>   // C++20: 자동 검증 로그 서식

using namespace DirectX;   // 이 파일 안에서만

// 자동 검증 (EditorApp 의 멤버 함수를 파일만 나눠 구현함). 실행 인자를 읽고(ParseAutomation), 정해진 프레임에
// 에디터의 같은 코드 경로를 부름(RunAutomation). 인자 목록과 뜻은 Editor/EditorAutomation.h 에 있음.

void EditorApp::ParseAutomation()
{
	if (!GetCommandLineOption(L"post-stages").empty())
	{
		DebugUI::SetShowPostStages(true);   // --post-stages=1 이면 후처리 단계별 썸네일 창을 처음부터 켬
	}
	const std::wstring computeBlur = GetCommandLineOption(L"compute-blur");   // --compute-blur=1|0: 가우시안 블러를 컴퓨트로 (RenderSettings::computeBlur)
	if (!computeBlur.empty())
	{
		GetEngine().GetRenderer().GetSettings().computeBlur = _wtoi(computeBlur.c_str()) != 0;
	}
	ApplyRenderPathOptions(GetEngine().GetRenderer().GetSettings());            // --deferred / --clustered / --shadow-priority / --ssao
	const std::wstring particleStep = GetCommandLineOption(L"particle-step");   // --particle-step=<초>: 파티클을 프레임마다 고정 시간만큼 (RenderSettings::particleFixedStep)
	if (!particleStep.empty())
	{
		GetEngine().GetRenderer().GetSettings().particleFixedStep = static_cast<float>(_wtof(particleStep.c_str()));
	}
	const std::wstring particleSort = GetCommandLineOption(L"particle-sort");   // --particle-sort=0: 알파 파티클을 정렬하지 않음 (RenderSettings::particleSort)
	if (!particleSort.empty())
	{
		GetEngine().GetRenderer().GetSettings().particleSort = particleSort != L"0";
	}
	const std::wstring exitAfter = GetCommandLineOption(L"exit-after");
	if (exitAfter.empty()) return;
	m_auto.active = true;
	m_auto.exitAfter = static_cast<uint64_t>(_wtoi64(exitAfter.c_str()));
	m_auto.avgFrames = static_cast<uint32_t>(_wtoi(GetCommandLineOption(L"avg-frames").c_str()));
	m_auto.selectName = Log::ToUtf8(GetCommandLineOption(L"select").c_str());
	const std::wstring pick = GetCommandLineOption(L"pick");
	if (!pick.empty())
	{
		m_auto.pick = swscanf_s(pick.c_str(), L"%f,%f", &m_auto.pickU, &m_auto.pickV) == 2;
	}
	const std::wstring position = GetCommandLineOption(L"set-position");
	if (!position.empty())
	{
		m_auto.setPosition = swscanf_s(position.c_str(), L"%f,%f,%f", &m_auto.position.x, &m_auto.position.y, &m_auto.position.z) == 3;
	}
	auto resolve = [](const std::wstring& path) -> std::wstring
	{
		if (path.empty() || path.find(L':') != std::wstring::npos || path[0] == L'\\') return path;
		return Paths::GetSceneDir() + path;   // 상대 경로는 씬 폴더 기준
	};
	m_auto.savePath = resolve(GetCommandLineOption(L"save"));
	m_auto.loadPath = resolve(GetCommandLineOption(L"load"));
	m_auto.screenshotPath = resolve(GetCommandLineOption(L"screenshot"));
	m_auto.dumpObjects = !GetCommandLineOption(L"dump-objects").empty();
	m_auto.debugView = _wtoi(GetCommandLineOption(L"debug-view").c_str());
	m_auto.lightIconsSelected = GetCommandLineOption(L"light-icons") == L"selected";
	m_auto.debugBones = _wtoi(GetCommandLineOption(L"debug-bones").c_str());
	{
		const std::string spec = Log::ToUtf8(GetCommandLineOption(L"tex-setting").c_str());   // 이름;압축;밉;최대크기
		const size_t first = spec.find(';');
		if (first != std::string::npos)
		{
			m_auto.texSettingName = spec.substr(0, first);
			int compress = 1;
			unsigned mips = 0, maxSize = 0;
			sscanf_s(spec.c_str() + first + 1, "%d;%u;%u", &compress, &mips, &maxSize);
			m_auto.texSetting = { compress != 0, mips, maxSize };
		}
	}
	m_auto.freezeCascadesAt = static_cast<uint64_t>(_wtoi64(GetCommandLineOption(L"freeze-cascades").c_str()));
	m_auto.freezeCullingAt = static_cast<uint64_t>(_wtoi64(GetCommandLineOption(L"freeze-culling").c_str()));
	const std::wstring freezeParts = GetCommandLineOption(L"freeze-parts");
	if (!freezeParts.empty())
	{
		m_auto.freezeParts[0] = freezeParts.find(L'c') != std::wstring::npos;
		m_auto.freezeParts[1] = freezeParts.find(L'o') != std::wstring::npos;
		m_auto.freezeParts[2] = freezeParts.find(L'l') != std::wstring::npos;
	}
	const std::wstring camLook = GetCommandLineOption(L"cam-look");
	if (!camLook.empty())
	{
		m_auto.camLook = swscanf_s(camLook.c_str(), L"%f,%f,%f;%f,%f,%f", &m_auto.camEye.x, &m_auto.camEye.y, &m_auto.camEye.z, &m_auto.camTarget.x, &m_auto.camTarget.y, &m_auto.camTarget.z) == 6;
	}
	m_auto.newScene = !GetCommandLineOption(L"new-scene").empty();
	m_auto.addPrimitive = GetCommandLineOption(L"add").empty() ? -1 : _wtoi(GetCommandLineOption(L"add").c_str());
	m_auto.browseDir = GetCommandLineOption(L"browse");
	const std::wstring drop = GetCommandLineOption(L"drop");
	if (!drop.empty())
	{
		const size_t semicolon = drop.find(L';');
		m_auto.dropPath = drop.substr(0, semicolon);
		if (semicolon != std::wstring::npos)
		{
			swscanf_s(drop.c_str() + semicolon + 1, L"%f,%f,%f", &m_auto.dropPosition.x, &m_auto.dropPosition.y, &m_auto.dropPosition.z);
		}
	}
	const std::wstring dropUv = GetCommandLineOption(L"drop-uv");
	if (!dropUv.empty())
	{
		m_auto.dropUv = swscanf_s(dropUv.c_str(), L"%f,%f", &m_auto.dropU, &m_auto.dropV) == 2;
	}
	m_auto.dropTexture = GetCommandLineOption(L"drop-texture");
	m_auto.play = !GetCommandLineOption(L"play").empty();
	m_auto.reloadScripts = !GetCommandLineOption(L"reload-scripts").empty();
	m_auto.stopAt = static_cast<uint64_t>(_wtoi64(GetCommandLineOption(L"stop-at").c_str()));
	m_auto.addComponent = Log::ToUtf8(GetCommandLineOption(L"add-component").c_str());
	{
		const std::wstring addUI = GetCommandLineOption(L"add-ui");
		m_auto.addUI = addUI.empty() ? -1 : _wtoi(addUI.c_str());
	}
	if (GetCommandLineOption(L"show-ui") == L"0")
	{
		m_editor.SetShowUI(false);
	}
	m_auto.newScript = Log::ToUtf8(GetCommandLineOption(L"new-script").c_str());
	m_auto.buildGame = Log::ToUtf8(GetCommandLineOption(L"build-game").c_str());
	m_auto.buildScene = Log::ToUtf8(GetCommandLineOption(L"build-scene").c_str());
	m_auto.newProject = Log::ToUtf8(GetCommandLineOption(L"new-project").c_str());
	m_auto.buildProject = !GetCommandLineOption(L"build-project").empty();
	m_auto.launcher = !GetCommandLineOption(L"launcher").empty();
	const std::wstring rotation = GetCommandLineOption(L"set-rotation");
	if (!rotation.empty())
	{
		m_auto.setRotation = swscanf_s(rotation.c_str(), L"%f,%f,%f", &m_auto.rotationDeg.x, &m_auto.rotationDeg.y, &m_auto.rotationDeg.z) == 3;
	}
	const std::wstring setParent = GetCommandLineOption(L"set-parent");
	if (!setParent.empty())
	{
		const size_t semicolon = setParent.find(L';');
		m_auto.parentChild = Log::ToUtf8(setParent.substr(0, semicolon).c_str());
		m_auto.parentName = semicolon != std::wstring::npos ? Log::ToUtf8(setParent.substr(semicolon + 1).c_str()) : std::string();
	}
	m_auto.deleteName = Log::ToUtf8(GetCommandLineOption(L"delete").c_str());
	m_auto.duplicateName = Log::ToUtf8(GetCommandLineOption(L"duplicate").c_str());
	m_auto.makePrefabName = Log::ToUtf8(GetCommandLineOption(L"make-prefab").c_str());
	const std::wstring instantiate = GetCommandLineOption(L"instantiate");
	if (!instantiate.empty())
	{
		const size_t semicolon = instantiate.find(L';');
		m_auto.instantiatePath = instantiate.substr(0, semicolon);
		if (semicolon != std::wstring::npos)
		{
			swscanf_s(instantiate.c_str() + semicolon + 1, L"%f,%f,%f", &m_auto.instantiatePosition.x, &m_auto.instantiatePosition.y, &m_auto.instantiatePosition.z);
		}
	}
	m_auto.undoCount = _wtoi(GetCommandLineOption(L"undo").c_str());
	m_auto.redoCount = _wtoi(GetCommandLineOption(L"redo").c_str());
	m_auto.saveAfterPath = resolve(GetCommandLineOption(L"save-after"));
	m_auto.gesture = !GetCommandLineOption(L"gesture").empty();
	// 움직임 플래그. 이름에 쉼표가 있어도 되게 숫자는 뒤에서부터 떼어 냄.
	const auto splitName = [](const std::wstring& spec, int numbers, std::string& name) -> std::wstring
	{
		size_t cut = spec.size();
		for (int i = 0; i < numbers && cut != std::wstring::npos && cut > 0; ++i)
		{
			cut = spec.rfind(L',', cut - 1);
		}
		if (cut == std::wstring::npos)
		{
			name.clear();
			return std::wstring();
		}
		name = Log::ToUtf8(spec.substr(0, cut).c_str());
		return spec.substr(cut + 1);
	};
	m_auto.camOrbitDeg = static_cast<float>(_wtof(GetCommandLineOption(L"cam-orbit").c_str()));
	const std::wstring spin = GetCommandLineOption(L"spin");
	if (!spin.empty())
	{
		m_auto.spinDeg = static_cast<float>(_wtof(splitName(spin, 1, m_auto.spinName).c_str()));
	}
	const std::wstring camMove = GetCommandLineOption(L"cam-move");
	if (!camMove.empty())
	{
		m_auto.camMove = swscanf_s(camMove.c_str(), L"%f,%f,%f", &m_auto.camMoveDelta.x, &m_auto.camMoveDelta.y, &m_auto.camMoveDelta.z) == 3;
	}
	const std::wstring move = GetCommandLineOption(L"move");
	if (!move.empty())
	{
		const std::wstring numbers = splitName(move, 3, m_auto.moveName);
		if (swscanf_s(numbers.c_str(), L"%f,%f,%f", &m_auto.moveDelta.x, &m_auto.moveDelta.y, &m_auto.moveDelta.z) != 3)
		{
			m_auto.moveName.clear();
		}
	}
	m_auto.eventFrame = static_cast<uint64_t>(_wtoi64(GetCommandLineOption(L"event-frame").c_str()));
	// 게임 UI·Rect 툴 입력 (EditorAutomation::InputEvent)
	{
		const auto split = [](const std::wstring& text)
		{
			std::vector<std::wstring> parts;
			size_t start = 0;
			while (start < text.size())
			{
				const size_t semi = text.find(L';', start);
				parts.push_back(text.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start));
				if (semi == std::wstring::npos) break;
				start = semi + 1;
			}
			return parts;
		};
		const auto keyCode = [](const std::wstring& name) -> uint32_t
		{
			static const std::pair<const wchar_t*, uint32_t> kKeys[] = { { L"Up", VK_UP }, { L"Down", VK_DOWN }, { L"Left", VK_LEFT }, { L"Right", VK_RIGHT }, { L"Enter", VK_RETURN }, { L"Space", VK_SPACE }, { L"Esc", VK_ESCAPE }, { L"Tab", VK_TAB } };
			for (const auto& k : kKeys)
			{
				if (name == k.first) return k.second;
			}
			if (name.size() == 1 && iswalnum(name[0])) return static_cast<uint32_t>(towupper(name[0]));
			return static_cast<uint32_t>(_wtoi(name.c_str()));
		};
		const auto add = [&](const wchar_t* option, EditorAutomation::InputEvent::Kind kind)
		{
			for (const std::wstring& part : split(GetCommandLineOption(option)))
			{
				const size_t at = part.rfind(L'@');
				if (at == std::wstring::npos) continue;
				EditorAutomation::InputEvent e;
				e.kind = kind;
				const std::wstring head = part.substr(0, at);
				const std::wstring tail = part.substr(at + 1);
				e.frame = static_cast<uint64_t>(_wtoi64(tail.c_str()));
				const size_t colon = tail.find(L':');
				if (colon != std::wstring::npos)
				{
					e.frames = static_cast<uint32_t>((std::max)(1, _wtoi(tail.c_str() + colon + 1)));
					const size_t colon2 = tail.find(L':', colon + 1);
					if (colon2 != std::wstring::npos)
					{
						const std::wstring mods = tail.substr(colon2 + 1);
						e.shift = mods.find(L's') != std::wstring::npos;
						e.alt = mods.find(L'a') != std::wstring::npos;
						e.ctrl = mods.find(L'c') != std::wstring::npos;
						e.right = mods.find(L'r') != std::wstring::npos;
					}
				}
				switch (kind)
				{
				case EditorAutomation::InputEvent::Key:
					e.vk = keyCode(head);
					break;
				case EditorAutomation::InputEvent::Pad:
				{
					static const std::pair<const wchar_t*, uint16_t> kPad[] = { { L"A", static_cast<uint16_t>(PadButton::A) }, { L"B", static_cast<uint16_t>(PadButton::B) }, { L"X", static_cast<uint16_t>(PadButton::X) }, { L"Y", static_cast<uint16_t>(PadButton::Y) },
						{ L"Up", static_cast<uint16_t>(PadButton::DPadUp) }, { L"Down", static_cast<uint16_t>(PadButton::DPadDown) }, { L"Left", static_cast<uint16_t>(PadButton::DPadLeft) }, { L"Right", static_cast<uint16_t>(PadButton::DPadRight) },
						{ L"Start", static_cast<uint16_t>(PadButton::Start) }, { L"Back", static_cast<uint16_t>(PadButton::Back) } };
					for (const auto& p : kPad)
					{
						if (head == p.first) e.pad = p.second;
					}
					if (head == L"StickUp") e.stickY = 1.0f;
					if (head == L"StickDown") e.stickY = -1.0f;
					if (head == L"StickLeft") e.stickX = -1.0f;
					if (head == L"StickRight") e.stickX = 1.0f;
					break;
				}
				case EditorAutomation::InputEvent::LogRect:
					e.name = Log::ToUtf8(head.c_str());
					break;
				default:
					swscanf_s(head.c_str(), L"%f,%f,%f,%f", &e.x0, &e.y0, &e.x1, &e.y1);
					if (kind == EditorAutomation::InputEvent::Wheel)
					{
						e.wheel = e.x1;
					}
					break;
				}
				m_auto.inputEvents.push_back(e);
			}
		};
		add(L"ui-click", EditorAutomation::InputEvent::Click);
		add(L"mouse", EditorAutomation::InputEvent::Mouse);
		add(L"ui-drag", EditorAutomation::InputEvent::Drag);
		add(L"ui-wheel", EditorAutomation::InputEvent::Wheel);
		add(L"key", EditorAutomation::InputEvent::Key);
		add(L"pad", EditorAutomation::InputEvent::Pad);
		add(L"tool-click", EditorAutomation::InputEvent::ToolClick);
		add(L"tool-drag", EditorAutomation::InputEvent::ToolDrag);
		add(L"log-rect", EditorAutomation::InputEvent::LogRect);
		m_auto.rectTool = GetCommandLineOption(L"tool") == L"rect";
		if (!m_auto.inputEvents.empty())
		{
			Log::Info("자동 검증: UI 입력 %zu 개", m_auto.inputEvents.size());
		}
	}
	Log::Info("자동 검증: exit-after %llu, pick %d, set-position %d, save '%s', load '%s', screenshot '%s', dump %d", m_auto.exitAfter, m_auto.pick ? 1 : 0, m_auto.setPosition ? 1 : 0, Log::ToUtf8(m_auto.savePath.c_str()).c_str(), Log::ToUtf8(m_auto.loadPath.c_str()).c_str(), Log::ToUtf8(m_auto.screenshotPath.c_str()).c_str(), m_auto.dumpObjects ? 1 : 0);
}

void EditorApp::RunAutomation()
{
	Engine& engine = GetEngine();
	Scene& scene = engine.GetScene();
	const uint64_t frame = engine.GetTime().GetFrameCount();
	const EditorAutomation& a = m_auto;

	// 한 줄 = (프레임, 조건, 동작). 이번 프레임이 그 프레임이고 조건이 참이면 동작을 실행함. 줄의 순서가 곧
	// 실행 순서이므로 같은 프레임의 동작끼리는 위에 적힌 것이 먼저임. 새 인자를 추가할 때는 EditorAutomation 에
	// 필드, ParseAutomation 에 읽기, 여기에 한 줄.
	const auto at = [frame](uint64_t when, bool enabled, auto&& action)
	{
		if (enabled && frame == when)
		{
			action();
		}
	};
	const auto selectedObject = [&]() -> GameObject*
	{
		const int selected = m_editor.GetSelectedObject();
		return scene.GetObject(selected < 0 ? SIZE_MAX : static_cast<size_t>(selected));
	};

	// --event-frame 이 있으면 한 번만 일어나는 동작을 그 프레임으로 옮김 (없으면 원래 프레임)
	const auto eventAt = [&a](uint64_t usual)
	{
		return a.eventFrame != 0 ? a.eventFrame : usual;
	};

	// ---- 프레임마다: 게임 UI·Rect 툴 가짜 입력 (scene.Update 보다 먼저라 이번 프레임 UISystem::Update 가 읽음) ----
	if (!a.inputEvents.empty())
	{
		Input& input = engine.GetInput();
		// 처음부터 가짜 입력 상태로 둠. 게임 UI 포인터 문(SetPointerViewport)은 지난 프레임의 IsSimulated 를 쓰므로
		// 첫 이벤트 프레임에 켜면 그 프레임의 누름을 놓침
		input.SetSimulated(true);
		const float ox = m_editor.GetSceneImageX();
		const float oy = m_editor.GetSceneImageY();
		const auto pointer = [&](float x, float y)
		{
			input.SetSimulated(true);
			input.SimulateMouseMove(static_cast<int>(std::lround(ox + x)), static_cast<int>(std::lround(oy + y)));
		};
		const auto logSelected = [&](const char* why)
		{
			const GameObject* selected = scene.FindById(engine.GetUI().GetSelectedId());
			Log::Info("자동 검증: %s 뒤 UI 선택 '%s'", why, selected != nullptr ? selected->GetName().c_str() : "(없음)");
		};
		const auto logRect = [&](const GameObject* object, const char* why)
		{
			const RectTransform* rt = object != nullptr ? object->GetBehaviour<RectTransform>() : nullptr;
			if (rt == nullptr)
			{
				Log::Warn("자동 검증: %s — RectTransform 없음", why);
				return;
			}
			XMFLOAT2 q[4] = {};
			const bool quad = engine.GetUI().GetScreenQuad(object, q);
			Log::Info("자동 검증: rect %s '%s' pos (%.3f, %.3f) size (%.3f, %.3f) pivot (%.3f, %.3f) anchors (%.3f, %.3f)-(%.3f, %.3f) screen %s (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f) (%.2f, %.2f)", why, object->GetName().c_str(),
				rt->anchoredPosition.x, rt->anchoredPosition.y, rt->sizeDelta.x, rt->sizeDelta.y, rt->pivot.x, rt->pivot.y, rt->anchorMin.x, rt->anchorMin.y, rt->anchorMax.x, rt->anchorMax.y,
				quad ? "" : "(없음)", q[0].x, q[0].y, q[1].x, q[1].y, q[2].x, q[2].y, q[3].x, q[3].y);
		};
		for (const EditorAutomation::InputEvent& e : a.inputEvents)
		{
			if (frame < e.frame) continue;
			const uint64_t k = frame - e.frame;
			switch (e.kind)
			{
			case EditorAutomation::InputEvent::Click:
				if (k == 0) { pointer(e.x0, e.y0); input.SimulateMouseButton(MouseButton::Left, true); Log::Info("자동 검증: ui-click (%.0f, %.0f)", e.x0, e.y0); }
				if (k == 1) input.SimulateMouseButton(MouseButton::Left, false);
				break;
			case EditorAutomation::InputEvent::Mouse:
				if (k == 0) pointer(e.x0, e.y0);
				break;
			case EditorAutomation::InputEvent::Drag:
				if (k == 0) { pointer(e.x0, e.y0); input.SimulateMouseButton(e.right ? MouseButton::Right : MouseButton::Left, true); }
				else if (k <= e.frames) { const float t = static_cast<float>(k) / static_cast<float>(e.frames); pointer(e.x0 + (e.x1 - e.x0) * t, e.y0 + (e.y1 - e.y0) * t); }
				else if (k == e.frames + 1) input.SimulateMouseButton(e.right ? MouseButton::Right : MouseButton::Left, false);
				break;
			case EditorAutomation::InputEvent::Wheel:
				if (k == 0) { pointer(e.x0, e.y0); input.SimulateWheel(e.wheel); }
				break;
			case EditorAutomation::InputEvent::Key:
				if (k == 0) { input.SetSimulated(true); input.SimulateKey(e.vk, true); }
				if (k == 1) input.SimulateKey(e.vk, false);
				if (k == 2) logSelected("key");
				break;
			case EditorAutomation::InputEvent::Pad:
				if (k == 0) { input.SetSimulated(true); input.SimulatePad(e.pad, e.stickX, e.stickY); }
				if (k == 1) input.SimulatePad(0, 0.0f, 0.0f);
				if (k == 2) logSelected("pad");
				break;
			case EditorAutomation::InputEvent::ToolClick:
				if (k == 0) m_editor.SetToolPointer(true, e.x0, e.y0, true);
				if (k == 1) m_editor.SetToolPointer(true, e.x0, e.y0, false);
				if (k == 2) { m_editor.SetToolPointer(false, e.x0, e.y0, false); logRect(scene.GetObject(m_editor.GetSelectedObject() < 0 ? SIZE_MAX : static_cast<size_t>(m_editor.GetSelectedObject())), "tool-click 선택"); }
				break;
			case EditorAutomation::InputEvent::ToolDrag:
			{
				const float t = k == 0 ? 0.0f : (std::min)(1.0f, static_cast<float>(k) / static_cast<float>((std::max)(1u, e.frames)));
				if (k <= e.frames) m_editor.SetToolPointer(true, e.x0 + (e.x1 - e.x0) * t, e.y0 + (e.y1 - e.y0) * t, true, e.shift, e.alt, e.ctrl);
				if (k == e.frames + 1) m_editor.SetToolPointer(true, e.x1, e.y1, false, e.shift, e.alt, e.ctrl);
				if (k == e.frames + 2) { m_editor.SetToolPointer(false, e.x1, e.y1, false); logRect(scene.GetObject(m_editor.GetSelectedObject() < 0 ? SIZE_MAX : static_cast<size_t>(m_editor.GetSelectedObject())), "tool-drag 뒤"); }
				break;
			}
			case EditorAutomation::InputEvent::LogRect:
				if (k == 0) logRect(scene.FindObject(e.name), "log");
				break;
			}
		}
	}
	at(2, a.rectTool, [&]
	{
		m_editor.SetGizmoOperation(3);
	});

	// ---- 프레임마다: 움직임 (속도 버퍼·모션 블러·TAA 검증). 2 프레임째부터 — 1 프레임째는 속도의 "이전"이 없음 ----
	if (frame >= 2)
	{
		Camera& camera = engine.GetCamera();
		if (a.camOrbitDeg != 0.0f)
		{
			camera.Rotate(XMConvertToRadians(a.camOrbitDeg), 0.0f);
		}
		if (a.camMove)
		{
			const XMFLOAT3& p = camera.GetPosition();
			camera.SetPosition(XMFLOAT3(p.x + a.camMoveDelta.x, p.y + a.camMoveDelta.y, p.z + a.camMoveDelta.z));
		}
		if (!a.spinName.empty() && m_playState == PlayState::Editing)
		{
			if (GameObject* object = scene.FindObject(a.spinName))
			{
				XMFLOAT3 r = object->GetTransform().GetRotation();
				r.y += XMConvertToRadians(a.spinDeg);
				object->GetTransform().SetRotation(r);
			}
			else if (frame == 2)
			{
				Log::Warn("자동 검증: spin 대상 '%s' 를 찾지 못함", a.spinName.c_str());
			}
		}
		if (!a.moveName.empty() && m_playState == PlayState::Editing)
		{
			if (GameObject* object = scene.FindObject(a.moveName))
			{
				object->GetTransform().Translate(a.moveDelta);
			}
			else if (frame == 2)
			{
				Log::Warn("자동 검증: move 대상 '%s' 를 찾지 못함", a.moveName.c_str());
			}
		}
	}

	// ---- 프레임이 인자로 정해지는 것 (CSM 디버그): 디버그 뷰(2) → 고정(N) → 카메라 이동(N+1, 고정이 없으면 3) → 종료 시 스크린샷 ----
	// --freeze-culling 이 있으면 그것을, 없으면 --freeze-cascades (캐스케이드만 — 예전 동작, docs/study/csm-study.html 의 명령)
	const uint64_t freezeAt = a.freezeCullingAt != 0 ? a.freezeCullingAt : a.freezeCascadesAt;
	at(freezeAt, freezeAt != 0, [&]
	{
		Renderer::CullFreezeParts parts;
		if (a.freezeCullingAt != 0)
		{
			parts = { .cascades = a.freezeParts[0], .objects = a.freezeParts[1], .lights = a.freezeParts[2] };
		}
		else
		{
			parts = { .cascades = true, .objects = false, .lights = false };
		}
		engine.GetRenderer().SetCullFreeze(true, engine.GetCamera(), parts);
		Log::Info("자동 검증: 컬링 고정 (cascades %d, objects %d, lights %d)", parts.cascades ? 1 : 0, parts.objects ? 1 : 0, parts.lights ? 1 : 0);
	});
	at(eventAt(freezeAt != 0 ? freezeAt + 1 : 3), a.camLook, [&]
	{
		engine.GetCamera().SetLookAt(a.camEye, a.camTarget);
	});

	// ---- 2: 설정·창 ----
	at(2, a.debugView != 0, [&]
	{
		engine.GetRenderer().GetSettings().debugView = a.debugView;
	});
	at(2, a.lightIconsSelected, [&]
	{
		m_editor.SetLightIconMode(Editor::LightIconMode::SelectedOnly);
	});
	at(12, !a.texSettingName.empty(), [&]
	{
		TextureImporter::SetSettings(a.texSettingName, a.texSetting);
		engine.GetRenderer().ReloadTexture(a.texSettingName);
		Log::Info("자동 검증: 텍스처 '%s' 설정 변경 (압축 %d, 밉 %u, 최대 %u)", a.texSettingName.c_str(), a.texSetting.compress ? 1 : 0, a.texSetting.mipLevels, a.texSetting.maxSize);
	});
	at(2, a.debugBones != 0, [&]
	{
		engine.GetRenderer().GetSettings().debugBones = a.debugBones;
	});
	at(2, !a.browseDir.empty(), [&]
	{
		m_editor.GetContentBrowser().SetDirectory(a.browseDir);
		m_editor.FocusContentBrowser();
	});
	at(2, !a.newScript.empty(), [&]
	{
		std::string error;
		if (ScriptCreator::Create(a.newScript, error))
		{
			Log::Info("자동 검증: 새 스크립트 %s 생성", a.newScript.c_str());
		}
		else
		{
			Log::Error("자동 검증: 새 스크립트 실패: %s", error.c_str());
		}
	});
	at(2, a.launcher, [&]
	{
		m_editor.ShowLauncher();
	});
	at(2, !a.newProject.empty(), [&]
	{
		std::string error;
		const bool ok = CreateProject(ProjectLauncher::GetDefaultProjectsDir(), a.newProject, error);
		Log::Info("자동 검증: new-project %s %s", a.newProject.c_str(), ok ? "성공" : ("실패: " + error).c_str());
	});

	// ---- 3: 새 씬, 게임 패키지 ----
	at(3, a.newScene, [&]
	{
		BuildEmptyScene();
	});
	at(3, !a.buildGame.empty(), [&]
	{
		GameBuilder::Options options;
		options.name = a.buildGame;
		options.startScene = a.buildScene.empty() ? (GetProject().startScene.empty() ? "demo_components.json" : GetProject().startScene) : a.buildScene;
		options.projectName = GetProject().GetName();
		options.projectSolution = GetProject().GetSolutionPath();
		options.openFolder = false;
		options.releaseBuild = !GetCommandLineOption(L"build-release").empty();   // Release 패키지 검증
		options.copyAllAssets = GetCommandLineOption(L"build-minimal").empty();   // --build-minimal=1 이면 시작 씬이 참조하는 에셋만 복사
		const GameBuilder::Result result = GameBuilder::Build(options);
		Log::Info("자동 검증: build-game %s → %s", result.ok ? "성공" : "실패", result.message.c_str());
	});

	// ---- 4: 스크립트 빌드, 오브젝트 추가 ----
	at(4, a.buildProject && GetProject().IsLoaded(), [&]
	{
		BuildScripts();
		Log::Info("자동 검증: build-project → 스크립트 DLL %s (컴포넌트 %zu개)", GetScripts().IsLoaded() ? "올라옴" : "없음", GetScripts().GetTypeNames().size());
	});
	at(4, a.addPrimitive >= 0, [&]
	{
		RunCommand("Add object", [&]()
		{
			AddPrimitive(a.addPrimitive);
			m_editor.SetSelectedObject(static_cast<int>(scene.GetObjects().size()) - 1);
		});
	});

	// ---- 5: 선택 ----
	at(eventAt(5), !a.selectName.empty(), [&]
	{
		int found = -1;
		for (size_t i = 0; i < scene.GetObjects().size() && found < 0; ++i)
		{
			if (scene.GetObjects()[i]->GetName() == a.selectName)
			{
				found = static_cast<int>(i);
			}
		}
		m_editor.SetSelectedObject(found);
		Log::Info("자동 검증: select '%s' → %d", a.selectName.c_str(), found);
	});
	at(5, a.pick, [&]
	{
		const int picked = m_editor.PickAt(scene, engine.GetCamera(), a.pickU, a.pickV);
		m_editor.SetSelectedObject(picked);
		Log::Info("자동 검증: pick (%.2f, %.2f) → %d %s", a.pickU, a.pickV, picked, picked >= 0 ? scene.GetObjects()[picked]->GetName().c_str() : "(none)");
	});

	// ---- 6: 모델 놓기, 삭제 — 씬 뷰 드롭·Delete 키가 부르는 함수를 그대로 부름 ----
	at(6, !a.dropPath.empty(), [&]
	{
		// 씬 뷰 드롭과 같은 경로. --drop-uv 가 있으면 광선 히트점(오브젝트 AABB 또는 y=0 바닥)에 놓음.
		XMFLOAT3 position = a.dropPosition;
		if (a.dropUv)
		{
			const int hit = m_editor.RaycastScene(scene, engine.GetCamera(), a.dropU, a.dropV, position);
			Log::Info("자동 검증: drop-uv (%.2f, %.2f) → 히트 (%.2f, %.2f, %.2f) %s", a.dropU, a.dropV, position.x, position.y, position.z, hit >= 0 ? scene.GetObjects()[hit]->GetName().c_str() : "(ground)");
		}
		RunCommand("Place model", [&]()
		{
			PlaceModel(a.dropPath, position);
		});
	});
	at(eventAt(6), !a.deleteName.empty(), [&]   // --event-frame 이면 그 프레임에 (재생 중 활성 카메라를 지워 카메라 컷 만들기)
	{
		GameObject* target = scene.FindObject(a.deleteName);
		if (target == nullptr)
		{
			Log::Warn("자동 검증: delete 대상 '%s' 를 찾지 못함", a.deleteName.c_str());
		}
		else
		{
			RunCommand("Delete object", [&]()
			{
				DeleteObject(static_cast<int>(scene.IndexOf(target)));
			});
		}
	});

	// ---- 7: 텍스처 드롭, 재부모화 ----
	at(7, !a.dropTexture.empty(), [&]
	{
		const int selected = m_editor.GetSelectedObject();
		if (selected >= 0 && selected < static_cast<int>(scene.GetObjects().size()))
		{
			const GameObject& object = *scene.GetObjects()[selected];
			Material* material = nullptr;
			for (auto& m : scene.GetMaterials())
			{
				if (m.get() == object.GetMaterial())
				{
					material = m.get();
				}
			}
			if (material != nullptr)
			{
				RunCommand("Drop texture", [&]()
				{
					material->albedoTexture = ContentBrowser::ToMaterialTextureName(a.dropTexture);
					Log::Info("자동 검증: '%s' 재질 '%s' 알베도 → %s", object.GetName().c_str(), material->name.c_str(), material->albedoTexture.c_str());
				});
			}
			else
			{
				Log::Warn("자동 검증: 선택 오브젝트에 편집 가능한 재질이 없어 drop-texture 를 건너뜀");
			}
		}
		else
		{
			Log::Warn("자동 검증: 선택된 오브젝트가 없어 drop-texture 를 건너뜀");
		}
	});
	at(7, !a.parentChild.empty(), [&]
	{
		// --set-parent=자식;부모 (부모가 비어 있으면 루트로). 월드 자세는 유지됨 (Hierarchy 드래그와 같은 경로).
		// set-rotation(8 프레임) 보다 먼저 실행해, 부모를 붙인 뒤 부모를 돌리면 자식(카메라)이 따라 도는지
		// 검증할 수 있게 함.
		GameObject* child = scene.FindObject(a.parentChild);
		GameObject* parent = a.parentName.empty() ? nullptr : scene.FindObject(a.parentName);
		if (child == nullptr || (parent == nullptr && !a.parentName.empty()))
		{
			Log::Warn("자동 검증: set-parent 대상을 찾지 못함 ('%s' → '%s')", a.parentChild.c_str(), a.parentName.c_str());
			return;
		}
		bool ok = false;
		RunCommand("Reparent", [&]()
		{
			ok = Editor::ReparentKeepingUI(&engine.GetUI(), *child, parent);   // UI 요소면 화면 사각형 유지 (Hierarchy 드롭과 같은 경로)
		});
		const XMFLOAT3 lp = child->GetTransform().GetPosition();
		const XMFLOAT3 wp = child->GetWorldPosition();
		Log::Info("자동 검증: set-parent '%s' → '%s' %s, 로컬 (%.2f, %.2f, %.2f) 월드 (%.2f, %.2f, %.2f)", a.parentChild.c_str(), parent != nullptr ? parent->GetName().c_str() : "(root)", ok ? "성공" : "거부(순환)", lp.x, lp.y, lp.z, wp.x, wp.y, wp.z);
	});

	// ---- 8: 선택 오브젝트의 위치·회전 ----
	at(8, a.setPosition, [&]
	{
		GameObject* object = selectedObject();
		if (object == nullptr)
		{
			Log::Warn("자동 검증: 선택된 오브젝트가 없어 set-position 을 건너뜀");
			return;
		}
		if (a.gesture)
		{
			m_forceBusy = true;   // 슬라이더를 잡고 끄는 중: 값은 바뀌지만 아직 단계로 기록되지 않음 (11 프레임째에 입력이 끝남)
			object->GetTransform().SetPosition(a.position);
		}
		else
		{
			RunCommand("Move", [&]()
			{
				object->GetTransform().SetPosition(a.position);
			});
		}
		Log::Info("자동 검증: '%s' 위치 → (%.2f, %.2f, %.2f)", object->GetName().c_str(), a.position.x, a.position.y, a.position.z);
	});
	at(8, a.setRotation, [&]
	{
		// 선택 오브젝트의 로컬 회전(도). roll 을 주고 카메라를 자식으로 붙이면 카메라 roll 상속을 검증할 수 있음.
		GameObject* object = selectedObject();
		if (object == nullptr)
		{
			Log::Warn("자동 검증: 선택된 오브젝트가 없어 set-rotation 을 건너뜀");
			return;
		}
		RunCommand("Rotate", [&]()
		{
			object->GetTransform().SetRotation(XMConvertToRadians(a.rotationDeg.x), XMConvertToRadians(a.rotationDeg.y), XMConvertToRadians(a.rotationDeg.z));
		});
		Log::Info("자동 검증: '%s' 회전 → (%.1f, %.1f, %.1f) deg", object->GetName().c_str(), a.rotationDeg.x, a.rotationDeg.y, a.rotationDeg.z);
	});

	at(5, a.reloadScripts, [&]
	{
		Log::Info("자동 검증: reload-scripts → %s", ReloadScripts() ? "성공" : "실패");
	});

	// ---- 9~10: 재생, 컴포넌트 추가 ----
	at(9, a.play, [&]
	{
		StartPlay();
	});
	at(10, !a.addComponent.empty(), [&]
	{
		GameObject* object = selectedObject();
		if (object == nullptr)
		{
			Log::Warn("자동 검증: 선택된 오브젝트가 없어 add-component 를 건너뜀");
			return;
		}
		Behaviour* added = nullptr;
		RunCommand("Add component", [&]()
		{
			added = object->AddBehaviour(a.addComponent);
		});
		Log::Info("자동 검증: '%s' 에 컴포넌트 %s %s", object->GetName().c_str(), a.addComponent.c_str(), added ? "추가" : "추가 실패");
	});

	at(10, a.addUI >= 0, [&]
	{
		int created = -1;
		const GameObject* selected = selectedObject();
		const int parentIndex = selected != nullptr ? static_cast<int>(scene.IndexOf(selected)) : -1;
		RunCommand(a.addUI == 0 ? "Add Canvas" : (a.addUI == 1 ? "Add UI Image" : "Add UI Text"), [&]()
		{
			created = AddUIElement(a.addUI, parentIndex);
		});
		if (created >= 0)
		{
			m_editor.SetSelectedObject(created);
		}
		Log::Info("자동 검증: add-ui %d → %d", a.addUI, created);
	});

	// ---- 11: 복제·프리팹 저장·프리팹 배치 — Ctrl+D·Save Prefab·씬 뷰 드롭이 부르는 함수를 그대로 부름. 끌기 흉내 끝 ----
	at(eventAt(11), !a.duplicateName.empty(), [&]
	{
		GameObject* target = scene.FindObject(a.duplicateName);
		if (target == nullptr)
		{
			Log::Warn("자동 검증: duplicate 대상 '%s' 를 찾지 못함", a.duplicateName.c_str());
		}
		else
		{
			RunCommand("Duplicate", [&]()
			{
				DuplicateObject(target->GetId());
			});
		}
	});
	at(11, !a.makePrefabName.empty(), [&]
	{
		GameObject* target = scene.FindObject(a.makePrefabName);
		if (target == nullptr)
		{
			Log::Warn("자동 검증: make-prefab 대상 '%s' 를 찾지 못함", a.makePrefabName.c_str());
		}
		else
		{
			RunCommand("Save prefab", [&]()
			{
				SavePrefabOf(target->GetId());
			});
		}
	});
	at(eventAt(11), !a.instantiatePath.empty(), [&]
	{
		RunCommand("Instantiate prefab", [&]()
		{
			InstantiatePrefabAt(a.instantiatePath, a.instantiatePosition);
		});
	});
	at(11, a.gesture, [&]
	{
		m_forceBusy = false;
	});

	// ---- 12~20: 저장 → 되돌리기 → 다시 하기 → 불러오기 → 다시 저장 → 덤프 ----
	at(12, !a.savePath.empty(), [&]
	{
		SaveSceneFile(a.savePath);
	});
	at(eventAt(14), a.undoCount > 0, [&]
	{
		for (int i = 0; i < a.undoCount; ++i)
		{
			UndoRedo(true);
		}
	});
	at(15, a.redoCount > 0, [&]
	{
		for (int i = 0; i < a.redoCount; ++i)
		{
			UndoRedo(false);
		}
	});
	at(eventAt(16), !a.loadPath.empty(), [&]
	{
		LoadSceneFile(a.loadPath);
	});
	at(18, !a.saveAfterPath.empty(), [&]
	{
		SaveSceneFile(a.saveAfterPath);
	});
	at(20, a.dumpObjects, [&]
	{
		const Camera& camera = engine.GetCamera();
		const CameraComponent* active = scene.GetActiveCamera();
		Log::Info("자동 검증: 카메라 위치 (%.2f, %.2f, %.2f) yaw %.2f pitch %.2f roll %.2f, 활성 카메라 %s", camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z, camera.GetYaw(), camera.GetPitch(), camera.GetRoll(), active != nullptr ? active->GetOwner().GetName().c_str() : "(editor)");
		for (const auto& object : scene.GetObjects())
		{
			const XMFLOAT3& p = object->GetTransform().GetPosition();
			const char* albedo = object->GetMaterial() != nullptr ? object->GetMaterial()->albedoTexture.c_str() : "";
			std::string components;
			for (const auto& b : object->GetBehaviours())
			{
				components += std::string(components.empty() ? "" : ",") + b->GetTypeName();
			}
			std::string parent;   // 부모가 있으면 이름과 월드 위치도 남김 (로컬 값과 비교용)
			if (object->GetParent() != nullptr)
			{
				const XMFLOAT3 wp = object->GetWorldPosition();
				parent = std::format(" parent='{}' world=({:.2f}, {:.2f}, {:.2f})", object->GetParent()->GetName(), wp.x, wp.y, wp.z);
			}
			Log::Info("자동 검증: 오브젝트 '%s' 위치 (%.2f, %.2f, %.2f) albedo=%s components=[%s]%s %s", object->GetName().c_str(), p.x, p.y, p.z, albedo, components.c_str(), parent.c_str(), m_playState == PlayState::Editing ? "(editing)" : "(playing)");
		}
	});

	// ---- 프레임이 인자로 정해지는 것: 정지, 스크린샷, 종료. 같은 프레임의 위 동작들이 끝난 뒤에 실행됨 ----
	at(a.stopAt, a.stopAt != 0, [&]
	{
		StopPlay();
	});
	at(a.exitAfter - 1, a.exitAfter > 1 && !a.screenshotPath.empty(), [&]
	{
		engine.RequestScreenshot(a.screenshotPath);
	});
	// --avg-frames: 종료 전 N 프레임 동안 구간 시간을 이름별로 더함 (CPU 샘플은 지난 프레임, GPU 는 몇 프레임 전 결과). 처음 본 순서를 유지함.
	static std::vector<std::pair<std::string, std::pair<double, uint32_t>>> averages;
	if (a.avgFrames > 0 && frame < a.exitAfter && frame + a.avgFrames >= a.exitAfter)
	{
		const auto add = [](const std::string& name, float ms)
		{
			for (auto& entry : averages)
			{
				if (entry.first == name)
				{
					entry.second.first += ms;
					++entry.second.second;
					return;
				}
			}
			averages.push_back({ name, { ms, 1u } });
		};
		for (const Profiler::CpuSample& sample : Profiler::GetCpuSamples())
		{
			add(std::string("cpu:") + sample.name, sample.milliseconds);
		}
		for (const Profiler::GpuSample& sample : Profiler::GetGpuSamples())
		{
			add(std::string("gpu:") + sample.name, sample.milliseconds);
		}
	}
	if (frame >= a.exitAfter)
	{
		// 렌더 경로 통계 (디퍼드·클러스터드·SSAO 검증용 — docs/plan/deferred-clustered-ssao-record.html 의 검증 절).
		const Renderer::Stats& s = engine.GetRenderer().GetStats();
		Log::Info("자동 검증: 렌더 통계 path=%s ssao=%d draws=%u culled=%u gbuffer=%u forwardOpaque=%u lightsVisible=%u lightsCulled=%u clusterRefs=%u clusterMax=%u overflow=%u clusterGpu=%d tileRefs=%u tileMax=%u shadowViews=%u offscreenShadowViews=%u skippedShadowLights=%u", s.deferredPath ? "deferred" : "forward", s.ssao ? 1 : 0, s.draws, s.culled, s.gbufferDraws, s.forwardOpaqueDraws, s.lightsVisible, s.lightsCulled, s.clusterLightRefs, s.clusterMaxLights, s.clusterOverflow, s.clusterGpu ? 1 : 0, s.clusterTileRefs, s.clusterTileMaxLights, s.shadowViews, s.shadowViewsOffscreen, engine.GetRenderer().GetSkippedShadowLights());
		std::string timings;
		for (const Profiler::CpuSample& sample : Profiler::GetCpuSamples())
		{
			timings += std::format(" cpu:{}={:.3f}", sample.name, sample.milliseconds);
		}
		for (const Profiler::GpuSample& sample : Profiler::GetGpuSamples())
		{
			timings += std::format(" gpu:{}={:.3f}", sample.name, sample.milliseconds);
		}
		Log::Info("자동 검증: 구간 ms%s", timings.c_str());
		if (!averages.empty())
		{
			std::string line;
			for (const auto& entry : averages)
			{
				line += std::format(" {}={:.3f}", entry.first, entry.second.first / entry.second.second);
			}
			Log::Info("자동 검증: 구간 평균 ms (%u 프레임)%s", a.avgFrames, line.c_str());
		}
		Log::Info("자동 검증: %llu 프레임 뒤 종료", frame);
		PostQuitMessage(0);
	}
}
