#include "Core/pch.h"
#include "Core/Editor/TextureViewer.h"
#include "Core/Graphics/Resource/ColorBuffer.h"
#include "Core/UI/ImGuiLayer.h"

#include <imgui.h>
#include <algorithm>
#include <vector>

namespace
{
    struct RegisteredTexture
    {
        std::string name;
        ColorBuffer* texture;
    };

    std::vector<RegisteredTexture> s_Textures;
    size_t s_Selected = 0;
    bool s_ShowPanel = false;

    const char* GetFormatName(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return "R32G32B32A32_FLOAT";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
        default: return "Other";
        }
    }

    void ShowTextureCombo(void)
    {
        if (!ImGui::BeginCombo("Texture", s_Textures[s_Selected].name.c_str()))
            return;

        for (size_t index = 0; index < s_Textures.size(); ++index)
        {
            const bool selected = index == s_Selected;
            if (ImGui::Selectable(s_Textures[index].name.c_str(), selected))
                s_Selected = index;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImVec2 FitToContentRegion(uint32_t width, uint32_t height)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        float scale = available.x / width;
        if (available.y > 0.0f)
            scale = std::min(scale, available.y / height);
        return ImVec2(width * scale, height * scale);
    }

    void ShowSelectedTexture(void)
    {
        ColorBuffer& texture = *s_Textures[s_Selected].texture;
        if (texture.GetResource() == nullptr)
        {
            ImGui::TextUnformatted("Not created");
            return;
        }

        ImGui::Text("%u x %u, %s", texture.GetWidth(), texture.GetHeight(), GetFormatName(texture.GetFormat()));
        ImGui::Image(ImGuiLayer::BindTexture(texture), FitToContentRegion(texture.GetWidth(), texture.GetHeight()));
    }

    void ShowContent(void)
    {
        if (s_Textures.empty())
        {
            ImGui::TextUnformatted("No textures");
            return;
        }

        ShowTextureCombo();
        ShowSelectedTexture();
    }
}

void TextureViewer::AddTexture(const std::string& name, ColorBuffer& texture)
{
    s_Textures.push_back({ name, &texture });
}

void TextureViewer::Show(void)
{
    ImGuiLayer::ShowPanel("Texture Viewer", s_ShowPanel, ShowContent);
}
