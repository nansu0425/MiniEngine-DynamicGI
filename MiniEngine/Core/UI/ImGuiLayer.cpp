#include "Core/pch.h"
#include "Core/UI/ImGuiLayer.h"
#include "Core/App/GameInput.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Command/CommandListManager.h"
#include "Core/Graphics/Descriptor/DescriptorHeap.h"
#include "Core/Graphics/Resource/BufferManager.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace GameCore
{
    extern HWND g_hWnd;
}

using namespace Graphics;

namespace
{
    // DXGI 기본 maximum frame latency(3)만큼 frame이 GPU에 쌓여 있어도
    // 지금 기록하는 frame이 그 frame들의 vertex buffer를 덮어쓰지 않게 하나 더 둔다.
    constexpr int kNumFramesInFlight = 4;

    // 100% DPI scale에서의 font 크기(pixel)
    constexpr float kBaseFontSize = 16.0f;
    constexpr float kDefaultFontSize = 13.0f;

    DescriptorHeap s_SrvHeap;
    float s_DpiScale = 0.0f;
    bool s_Visible = true;
    bool s_ShowDemoWindow = false;

    void AllocSrvDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
    {
        DescriptorHandle handle = s_SrvHeap.Alloc();
        *outCpuHandle = handle;
        *outGpuHandle = handle;
    }

    // DescriptorHeap은 개별 해제를 지원하지 않는다. Shutdown에서 heap을 통째로 해제한다.
    void FreeSrvDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
    {
    }

    // bitmap font인 기본 font는 확대하면 흐려지므로 Windows에 있는 vector font를 쓴다.
    void LoadFonts(float dpiScale)
    {
        ImFontAtlas* fonts = ImGui::GetIO().Fonts;
        fonts->Clear();

        char fontPath[MAX_PATH];
        GetWindowsDirectoryA(fontPath, MAX_PATH);
        strcat_s(fontPath, "\\Fonts\\segoeui.ttf");
        if (GetFileAttributesA(fontPath) != INVALID_FILE_ATTRIBUTES)
        {
            fonts->AddFontFromFileTTF(fontPath, kBaseFontSize * dpiScale);
        }
        else
        {
            ImFontConfig config;
            config.SizePixels = kDefaultFontSize * dpiScale;
            fonts->AddFontDefault(&config);
        }
    }

    void ApplyDpiScale(float dpiScale)
    {
        LoadFonts(dpiScale);

        ImGuiStyle& style = ImGui::GetStyle();
        style = ImGuiStyle();
        style.ScaleAllSizes(dpiScale);

        s_DpiScale = dpiScale;
    }

    void DrawMainMenu(void)
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("ImGui Demo", nullptr, &s_ShowDemoWindow);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }
}

void ImGuiLayer::Initialize(void)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplWin32_Init(GameCore::g_hWnd);
    ApplyDpiScale(ImGui_ImplWin32_GetDpiScaleForHwnd(GameCore::g_hWnd));

    s_SrvHeap.Create(L"ImGui SRV Descriptors", D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64);

    ImGui_ImplDX12_InitInfo initInfo;
    initInfo.Device = g_Device;
    initInfo.CommandQueue = g_CommandManager.GetCommandQueue();
    initInfo.NumFramesInFlight = kNumFramesInFlight;
    initInfo.RTVFormat = g_OverlayBuffer.GetFormat();
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = s_SrvHeap.GetHeapPointer();
    initInfo.SrvDescriptorAllocFn = AllocSrvDescriptor;
    initInfo.SrvDescriptorFreeFn = FreeSrvDescriptor;
    ImGui_ImplDX12_Init(&initInfo);
}

void ImGuiLayer::Shutdown(void)
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    s_SrvHeap.Destroy();
}

void ImGuiLayer::BeginFrame(void)
{
    // DPI가 다른 monitor로 window를 옮기면 그 DPI에 맞춰 font texture를 다시 만든다.
    const float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(GameCore::g_hWnd);
    if (dpiScale != s_DpiScale)
    {
        // 이전 frame이 font texture를 다 쓴 뒤에 해제해야 한다.
        g_CommandManager.IdleGPU();
        ImGui_ImplDX12_InvalidateDeviceObjects();
        ApplyDpiScale(dpiScale);
    }

    ImGuiIO& io = ImGui::GetIO();
    const bool mouseLook = GameInput::IsMouseLookActive();

    // UI가 숨겨져 있거나 camera를 조작하는 중에는 ImGui가 입력에 반응하지 않게 한다.
    const ImGuiConfigFlags inputBlockFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
    if (!s_Visible || mouseLook)
        io.ConfigFlags |= inputBlockFlags;
    else
        io.ConfigFlags &= ~inputBlockFlags;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (mouseLook)
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    GameInput::SetCapturedByUI(io.WantCaptureMouse, io.WantCaptureKeyboard);

    if (s_Visible)
    {
        DrawMainMenu();
        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        if (s_ShowDemoWindow)
            ImGui::ShowDemoWindow(&s_ShowDemoWindow);
    }
}

void ImGuiLayer::Render(void)
{
    ImGui::Render();
    if (!s_Visible)
        return;

    GraphicsContext& context = GraphicsContext::Begin(L"Render ImGui");
    context.TransitionResource(g_OverlayBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    context.SetRenderTarget(g_OverlayBuffer.GetRTV());
    context.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, s_SrvHeap.GetHeapPointer());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), context.GetCommandList());
    context.Finish();
}

bool ImGuiLayer::HandleWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // 숨긴 동안에는 ImGui가 keyboard 입력을 받지 않으므로 toggle은 ImGui 밖에서 처리한다.
    // lParam의 bit 30은 key repeat이다.
    if (message == WM_KEYDOWN && wParam == VK_F1 && (lParam & (1 << 30)) == 0)
    {
        s_Visible = !s_Visible;
        return true;
    }

    return ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam) != 0;
}
