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
#include "Core/Editor/EditorSettings.h"
#include "Core/Automation/ControlChannel.h"
#include "Core/Automation/FrameCapture.h"
#include "Core/Automation/UnattendedRun.h"
#include <shellapi.h>

#pragma comment(lib, "runtimeobject.lib")

namespace GameCore
{
    using namespace Graphics;

    bool gIsSupending = false;

    extern HWND g_hWnd;

    // 창의 크기가 scene 해상도를 정하므로, 다음 실행 때 같은 해상도로 잴 수 있게 창의 배치를 저장한다.
    namespace WindowSettings
    {
        // 창을 닫아 message loop가 끝나면 Save 때 창이 이미 없으므로, 없어지기 직전의 배치를 둔다.
        WINDOWPLACEMENT s_DestroyedPlacement = { sizeof(WINDOWPLACEMENT) };
        // editor.ini에서 되돌린, 창이 처음 보일 때의 상태. 없으면 음수다.
        int s_ShowCommand = -1;

        bool IsMaximized(const WINDOWPLACEMENT& placement)
        {
            return placement.showCmd == SW_SHOWMAXIMIZED ||
                (placement.showCmd == SW_SHOWMINIMIZED && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0);
        }

        void Save(EditorSettings::Values& values)
        {
            WINDOWPLACEMENT placement = { sizeof(placement) };
            if (!GetWindowPlacement(g_hWnd, &placement))
                placement = s_DestroyedPlacement;

            const RECT& rect = placement.rcNormalPosition;
            char text[64];
            sprintf_s(text, "%ld,%ld,%ld,%ld", rect.left, rect.top, rect.right, rect.bottom);
            values["Normal Position"] = text;
            values["Maximized"] = IsMaximized(placement) ? "1" : "0";
        }

        // 창이 보이기 전에 부르므로 위치만 바꾸고, 최대화는 처음 보일 때 한다.
        void Load(const EditorSettings::Values& values)
        {
            const auto position = values.find("Normal Position");
            RECT rect = {};
            if (position == values.end() ||
                sscanf_s(position->second.c_str(), "%ld,%ld,%ld,%ld", &rect.left, &rect.top, &rect.right, &rect.bottom) != 4)
                return;

            WINDOWPLACEMENT placement = { sizeof(placement) };
            GetWindowPlacement(g_hWnd, &placement);
            placement.rcNormalPosition = rect;
            placement.showCmd = SW_HIDE;
            SetWindowPlacement(g_hWnd, &placement);

            const auto maximized = values.find("Maximized");
            s_ShowCommand = maximized != values.end() && maximized->second == "1" ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        }
    }

    void InitializeApplication( IGameApp& game )
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        CommandLineArgs::Initialize(argc, argv);
        Utility::InitializeLogFile();
        // device를 찾지 못해 멈추는 것처럼 초기화 중의 실패도 무인 실행에서 판정할 수 있게 가장 먼저 건다.
        UnattendedRun::Initialize();

        Graphics::Initialize(game.RequiresInlineRaytracing());
        SystemTime::Initialize();
        GameInput::Initialize();
        EngineTuning::Initialize();
        ImGuiLayer::Initialize();
        EditorSettings::Register("Window", WindowSettings::Save, WindowSettings::Load);

        game.Startup();
        // Startup이 정한 기본값을 지난 실행의 상태로 덮어쓴다.
        EditorSettings::Load();
        ControlChannel::Initialize();
    }

    void TerminateApplication( IGameApp& game )
    {
        ControlChannel::Shutdown();
        g_CommandManager.IdleGPU();

        EditorSettings::Save();
        game.Cleanup();

        ImGuiLayer::Shutdown();

        GameInput::Shutdown();
    }

    bool UpdateApplication( IGameApp& game )
    {
        EngineProfiling::Update();

        float DeltaTime = Graphics::GetFrameTime();

        // harness의 명령은 F1처럼 frame을 시작하기 전에 적용해 이번 frame의 UI, 입력, update가 모두 같은 상태를 보게 한다.
        ControlChannel::Update();
        // GameInput이 UI가 가져간 입력을 걸러낼 수 있게 ImGui frame을 먼저 연다.
        ImGuiLayer::BeginFrame();
        // scene을 rendering하기 전에 rendering buffer를 이번 frame의 scene 영역 크기에 맞춘다.
        const RECT sceneViewport = ImGuiLayer::GetSceneViewport();
        Display::SetSceneViewport(sceneViewport.left, sceneViewport.top,
            sceneViewport.right - sceneViewport.left, sceneViewport.bottom - sceneViewport.top);
        GameInput::Update(DeltaTime);
        EngineTuning::Update();

        game.Update(DeltaTime);
        game.RenderScene();
        FrameCapture::Capture(FrameCapture::kScene, g_SceneColorBuffer);

        PostEffects::Render();

        GraphicsContext& OverlayContext = GraphicsContext::Begin(L"Render Scene Overlay");
        OverlayContext.TransitionResource(g_SceneOverlayBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
        OverlayContext.ClearColor(g_SceneOverlayBuffer);
        OverlayContext.SetRenderTarget(g_SceneOverlayBuffer.GetRTV());
        OverlayContext.SetViewportAndScissor(0, 0, g_SceneOverlayBuffer.GetWidth(), g_SceneOverlayBuffer.GetHeight());
        game.RenderSceneOverlay(OverlayContext);
        OverlayContext.Finish();
        FrameCapture::Capture(FrameCapture::kOverlay, g_SceneOverlayBuffer);

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
        // 처음 부른 ShowWindow는 app을 실행한 program이 정한 상태를 따를 수 있으므로, 되돌린 상태는 그 뒤에 적용한다.
        if (WindowSettings::s_ShowCommand >= 0)
            ShowWindow(g_hWnd, WindowSettings::s_ShowCommand);

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
        UnattendedRun::ReportNormalExit();
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
            GetWindowPlacement(hWnd, &WindowSettings::s_DestroyedPlacement);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc( hWnd, message, wParam, lParam );
        }

        return 0;
    }

}
