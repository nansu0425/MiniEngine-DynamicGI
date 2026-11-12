#pragma once

#include <string>

class ColorBuffer;

namespace TextureViewer
{
    void AddTexture(const std::string& name, ColorBuffer& texture);

    void Show(void);
}
