//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#include "Core/pch.h"
#include "Core/App/GameCore.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/App/SystemTime.h"
#include "Core/App/GameInput.h"
#include "Core/Graphics/Resource/BufferManager.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Effects/PostEffects/PostEffects.h"
#include "Core/Display/Display.h"
#include "Core/UI/ImGuiLayer.h"
#include "Core/Utility/CommandLineArg.h"
#include <shellapi.h>

#pragma comment(lib, "runtimeobject.lib") 

namespace GameCore
{
    using namespace Graphics;

    bool gIsSupending = false;

    void InitializeApplication( IGameApp& game )
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        CommandLineArgs::Initialize(argc, argv);

        Graphics::Initialize(game.RequiresInlineRaytracing());
        SystemTime::Initialize();
        GameInput::Initialize();
        EngineTuning::Initialize();
        ImGuiLayer::Initialize();

        game.Startup();
    }

    void TerminateApplication( IGameApp& game )
    {
        g_CommandManager.IdleGPU();

        game.Cleanup();

        ImGuiLayer::Shutdown();

        GameInput::Shutdown();
    }

    bool UpdateApplication( IGameApp& game )
    {
        EngineProfiling::Update();

        float DeltaTime = Graphics::GetFrameTime();

        // GameInput이 UI가 가져간 입력을 걸러낼 수 있게 ImGui frame을 먼저 연다.
        ImGuiLayer::BeginFrame();
        // scene을 rendering하기 전에 rendering buffer를 이번 frame의 scene 영역 크기에 맞춘다.
        const RECT sceneViewport = ImGuiLayer::GetSceneViewport();
        Display::SetSceneViewport(sceneViewport.left, sceneViewport.top,
            sceneViewport.right - sceneViewport.left, sceneViewport.bottom - sceneViewport.top);
        GameInput::Update(DeltaTime);
        EngineTuning::Update(DeltaTime);
        
        game.Update(DeltaTime);
        game.RenderScene();

        PostEffects::Render();

        GraphicsContext& UiContext = GraphicsContext::Begin(L"Render UI");
        UiContext.TransitionResource(g_SceneOverlayBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
        UiContext.ClearColor(g_SceneOverlayBuffer);
        UiContext.SetRenderTarget(g_SceneOverlayBuffer.GetRTV());
        UiContext.SetViewportAndScissor(0, 0, g_SceneOverlayBuffer.GetWidth(), g_SceneOverlayBuffer.GetHeight());
        game.RenderUI(UiContext);

        UiContext.SetRenderTarget(g_SceneOverlayBuffer.GetRTV());
        UiContext.SetViewportAndScissor(0, 0, g_SceneOverlayBuffer.GetWidth(), g_SceneOverlayBuffer.GetHeight());
        EngineTuning::Display( UiContext, 10.0f, 40.0f, 1900.0f, 1040.0f );

        UiContext.Finish();

        EngineProfiling::ShowFrameStats();
        EngineProfiling::ShowProfiler();
        ImGuiLayer::Render();

        Display::Present();

        return !game.IsDone();
    }

    // Default implementation to be overridden by the application
    bool IGameApp::IsDone( void )
    {
        return GameInput::IsFirstPressed(GameInput::kKey_escape);
    }

    HWND g_hWnd = nullptr;

    LRESULT CALLBACK WndProc( HWND, UINT, WPARAM, LPARAM );

    int RunApplication( IGameApp& app, const wchar_t* className, HINSTANCE hInst, int nCmdShow )
    {
        if (!XMVerifyCPUSupport())
            return 1;

        Microsoft::WRL::Wrappers::RoInitializeWrapper InitializeWinRT(RO_INIT_MULTITHREADED);
        ASSERT_SUCCEEDED(InitializeWinRT);

        // 이 설정이 없으면 Windows가 저해상도로 그린 화면을 DPI scale만큼 늘려서 흐리게 보여 준다.
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        // Register class
        WNDCLASSEX wcex;
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = hInst;
        wcex.hIcon = LoadIcon(hInst, IDI_APPLICATION);
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszMenuName = nullptr;
        wcex.lpszClassName = className;
        wcex.hIconSm = LoadIcon(hInst, IDI_APPLICATION);
        ASSERT(0 != RegisterClassEx(&wcex), "Unable to register a window");

        // Create window
        RECT rc = { 0, 0, (LONG)g_DisplayWidth, (LONG)g_DisplayHeight };
        AdjustWindowRectExForDpi(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());

        g_hWnd = CreateWindow(className, className, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInst, nullptr);

        ASSERT(g_hWnd != 0);

        InitializeApplication(app);

        ShowWindow( g_hWnd, nCmdShow/*SW_SHOWDEFAULT*/ );

        do
        {
            MSG msg = {};
            bool done = false;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);

                if (msg.message == WM_QUIT)
                    done = true;
            }

            if (done)
                break;
        }
        while (UpdateApplication(app));	// Returns false to quit loop

        TerminateApplication(app);
        Graphics::Shutdown();
        return 0;
    }

    //--------------------------------------------------------------------------------------
    // Called every time the application receives a message
    //--------------------------------------------------------------------------------------
    LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
    {
        if (ImGuiLayer::HandleWindowMessage(hWnd, message, wParam, lParam))
            return TRUE;

        switch( message )
        {
        case WM_SIZE:
            Display::Resize((UINT)(UINT64)lParam & 0xFFFF, (UINT)(UINT64)lParam >> 16);
            break;

        case WM_DPICHANGED:
        {
            // 옮겨 간 monitor에서도 같은 크기로 보이도록 Windows가 제안한 크기로 바꾼다.
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            break;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc( hWnd, message, wParam, lParam );
        }

        return 0;
    }

}
