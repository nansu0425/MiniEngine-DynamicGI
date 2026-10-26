#include "Core/pch.h"
#include "Core/Automation/FrameCapture.h"
#include "Core/Display/Display.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Resource/PixelBuffer.h"
#include "Core/Graphics/Resource/ReadbackBuffer.h"

#include "DirectXTex.h"
#include <wincodec.h>

#include <fstream>
#include <map>

using namespace FrameCapture;

namespace
{
    struct TargetDesc
    {
        const char* name;
        const char* extension;
        DXGI_FORMAT fileFormat;
    };

    const TargetDesc kTargets[] =
    {
        { "scene", ".dds", DXGI_FORMAT_R32G32B32A32_FLOAT },
        { "overlay", ".png", DXGI_FORMAT_R8G8B8A8_UNORM },
        { "display", ".png", DXGI_FORMAT_R8G8B8A8_UNORM },
        { "pt", ".dds", DXGI_FORMAT_R32G32B32A32_FLOAT },
    };
    static_assert(_countof(kTargets) == kNumTargets, "target 개수가 Target과 다르다");

    struct PendingCapture
    {
        std::string pathWithoutExtension;
        ControlChannel::Reply reply;
    };

    std::map<Target, PendingCapture> s_PendingCaptures;

    HRESULT Save(Target target, const DirectX::Image& image, const std::wstring& path)
    {
        using namespace DirectX;

        const DXGI_FORMAT fileFormat = kTargets[target].fileFormat;
        ScratchImage converted;
        const Image* fileImage = &image;
        if (image.format != fileFormat)
        {
            const HRESULT hr = Convert(image, fileFormat, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted);
            if (FAILED(hr))
                return hr;
            fileImage = converted.GetImage(0, 0, 0);
        }

        if (target == kScene || target == kPathTracer)
            return SaveToDDSFile(*fileImage, DDS_FLAGS_NONE, path.c_str());

        // present는 backbuffer의 alpha를 쓰지 않아 그 값이 정해져 있지 않으므로 버린다.
        const GUID* pixelFormat = target == kDisplay ? &GUID_WICPixelFormat24bppBGR : nullptr;
        return SaveToWICFile(*fileImage, WIC_FLAGS_NONE, GetWICCodec(WIC_CODEC_PNG), path.c_str(), pixelFormat);
    }

    // image를 참고로 볼 때 판정할 근거가 함께 있도록, 그 image를 그린 frame의 state를 옆에 남긴다.
    bool SaveState(const std::string& path)
    {
        std::ofstream file(Utility::ToWide(path), std::ios::binary);
        file << "{" << ControlChannel::GetState().ToMembers() << "}";
        return file.good();
    }
}

bool FrameCapture::FindTarget(const std::string& name, Target& target)
{
    for (int index = 0; index < kNumTargets; ++index)
    {
        if (name == kTargets[index].name)
        {
            target = (Target)index;
            return true;
        }
    }
    return false;
}

void FrameCapture::Request(Target target, const std::string& pathWithoutExtension, const ControlChannel::Reply& reply)
{
    if (s_PendingCaptures.find(target) != s_PendingCaptures.end())
    {
        reply.Error(std::string("a capture of ") + kTargets[target].name + " is already waiting");
        return;
    }
    s_PendingCaptures.emplace(target, PendingCapture{ pathWithoutExtension, reply });
}

void FrameCapture::Capture(Target target, PixelBuffer& buffer)
{
    if (s_PendingCaptures.find(target) == s_PendingCaptures.end())
        return;

    CommandContext& context = CommandContext::Begin(L"Frame Capture");
    Capture(target, context, buffer);
    context.Finish();
}

void FrameCapture::Capture(Target target, CommandContext& context, PixelBuffer& buffer)
{
    const auto pending = s_PendingCaptures.find(target);
    if (pending == s_PendingCaptures.end())
        return;

    const std::string path = pending->second.pathWithoutExtension + kTargets[target].extension;
    const std::string statePath = pending->second.pathWithoutExtension + ".state.json";
    const ControlChannel::Reply reply = pending->second.reply;
    s_PendingCaptures.erase(pending);

    if (buffer.GetResource() == nullptr)
    {
        reply.Error(std::string(kTargets[target].name) + " has not been drawn yet");
        return;
    }

    ReadbackBuffer readback;
    const uint32_t rowPitch = context.ReadbackTexture(readback, buffer);
    context.Flush(true);

    DirectX::Image image = {};
    image.width = buffer.GetWidth();
    image.height = buffer.GetHeight();
    image.format = buffer.GetFormat();
    image.rowPitch = rowPitch;
    image.slicePitch = (size_t)rowPitch * buffer.GetHeight();
    image.pixels = (uint8_t*)readback.Map();
    const HRESULT hr = Save(target, image, Utility::ToWide(path));
    readback.Unmap();

    if (FAILED(hr))
    {
        char hrText[16];
        sprintf_s(hrText, "0x%08X", (unsigned)hr);
        reply.Error("cannot write " + path + " (hr " + hrText + ")");
        return;
    }
    if (!SaveState(statePath))
    {
        reply.Error("cannot write " + statePath);
        return;
    }
    reply.Ok(ControlChannel::Fields()
        .Text("path", path)
        .Integer("width", buffer.GetWidth())
        .Integer("height", buffer.GetHeight())
        .Integer("frame", Graphics::GetFrameCount())
        .Text("state", statePath));
}
