#include "pch.h"
#include "App/TestApp.h"
#include <imgui.h>

TestApp::TestApp()
{

}

bool TestApp::Initialize()
{
	if (!AppBase::Initialize()) return false;
	return true;
}

void TestApp::UpdateGUI()
{
	ImGui::Text("dt %.3f ms  total %.1f s", m_lastDt * 1000.0f, m_totalTime);
	renderer.UpdateGUI();
}

void TestApp::Update(float dt)
{
	m_lastDt = dt;
}

void TestApp::Render()
{
	shaderClass.BindConstantBuffers();
	renderer.Render();
}
