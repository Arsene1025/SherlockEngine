#include "pch.h"
#include "TestApp.h"

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
	renderer.UpdateGUI();
}

void TestApp::Update()
{
	//gameTime 전역변수
}

void TestApp::Render()
{
	shaderClass.ShaderUpdate();
	renderer.RenderModeUpdate();
	renderer.Render();
}
