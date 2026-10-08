#include "pch.h"
#include "App/EntryPoints.h"
#include "App/EditorApp.h"
#include "App/GameApp.h"
#include "Core/Log.h"

int RunEditor()
{
	EditorApp app;
	if (!app.Initialize())
	{
		Log::Error("App 초기화 실패!");
		return -1;
	}
	return app.Run();
}

int RunGame()
{
	GameApp app;
	if (!app.Initialize())
	{
		Log::Error("게임 초기화 실패!");
		return -1;
	}
	return app.Run();
}
