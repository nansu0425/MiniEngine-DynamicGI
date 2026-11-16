#pragma once

#include <DirectXMath.h>
#include <functional>
#include <optional>
#include <string>

class ColorBuffer;

namespace TextureViewer
{
    struct Annotation
    {
        std::string markedRowName;
        std::function<std::optional<uint32_t>(void)> findMarkedRow;
        std::function<std::string(DirectX::XMUINT2 texel)> describeTexel;
    };

    void Initialize(void);
    void Shutdown(void);

    void AddTexture(const std::string& name, ColorBuffer& texture, const Annotation& annotation = {});

    void Show(void);
}
