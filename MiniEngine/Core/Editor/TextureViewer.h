#pragma once

#include <string>

class ColorBuffer;

namespace TextureViewer
{
    void Initialize(void);
    void Shutdown(void);

    void AddTexture(const std::string& name, ColorBuffer& texture);

    void Show(void);
}
