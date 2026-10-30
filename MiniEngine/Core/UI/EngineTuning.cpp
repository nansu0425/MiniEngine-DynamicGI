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
#include "Core/UI/ImGuiLayer.h"
#include "Core/Editor/EditorSettings.h"
#include <imgui.h>

using namespace std;

namespace EngineTuning
{
    // For delayed registration.  Some objects are constructed before we can add them to the graph (due
    // to unreliable order of initialization.)
    enum { kMaxUnregisteredTweaks = 1024 };
    char s_UnregisteredPath[kMaxUnregisteredTweaks][128];
    EngineVar* s_UnregisteredVariable[kMaxUnregisteredTweaks] = { nullptr };
    int32_t s_UnregisteredCount = 0;

    // Internal functions
    void AddToVariableGraph( const string& path, EngineVar& var );
    void RegisterVariable( const string& path, EngineVar& var );

    bool s_ShowPanel = false;
}

// Not open to the public.  Groups are auto-created when a tweaker's path includes the group name.
class VariableGroup : public EngineVar
{
public:
    EngineVar* FindChild( const string& name )
    {
        auto iter = m_Children.find(name);
        return iter == m_Children.end() ? nullptr : iter->second;
    }

    void AddChild( const string& name, EngineVar& child )
    {
        m_Children[name] = &child;
        child.m_GroupPtr = this;
    }

    void SetSection( const TuningSection& section )
    {
        ASSERT(m_Section == nullptr, "A group has only one section");
        m_Section = &section;
    }

    void ShowChildren( void );

    // 변수의 key는 root group부터의 경로다. 그래서 변수가 더해지거나 빠져도 나머지 변수의 값은 그대로 되돌린다.
    void SaveValues( EditorSettings::Values& values, const string& pathPrefix ) const;
    void LoadValues( const EditorSettings::Values& values, const string& pathPrefix );

    virtual void SetFromString( const std::string& ) override {}

    static VariableGroup sm_RootGroup;

private:
    void ShowContent( void );

    std::map<string, EngineVar*> m_Children;
    const TuningSection* m_Section = nullptr;
};

VariableGroup VariableGroup::sm_RootGroup;

//=====================================================================================================================
// VariableGroup implementation

void VariableGroup::ShowChildren( void )
{
    for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
    {
        ImGui::PushID(iter->second);

        VariableGroup* subGroup = dynamic_cast<VariableGroup*>(iter->second);
        if (subGroup != nullptr)
        {
            if (ImGui::TreeNode(iter->first.c_str()))
            {
                subGroup->ShowContent();
                ImGui::TreePop();
            }
        }
        else
        {
            iter->second->Edit(iter->first.c_str());
        }

        ImGui::PopID();
    }
}

void VariableGroup::ShowContent( void )
{
    if (m_Section != nullptr)
    {
        // section의 widget이 group 아래 변수의 widget과 label이 같아도 부딪히지 않게 한다.
        ImGui::PushID(m_Section);
        m_Section->Show();
        ImGui::PopID();
    }
    ShowChildren();
}

void VariableGroup::SaveValues( EditorSettings::Values& values, const string& pathPrefix ) const
{
    if (m_Section != nullptr)
    {
        TuningSection::Values sectionValues;
        m_Section->Save(sectionValues);
        for (const auto& value : sectionValues)
        {
            ASSERT(value.first.find('/') == string::npos && m_Children.find(value.first) == m_Children.end(),
                "A section key is a variable path: %s%s", pathPrefix.c_str(), value.first.c_str());
            values[pathPrefix + value.first] = value.second;
        }
    }

    for (const auto& child : m_Children)
    {
        const string path = pathPrefix + child.first;
        const VariableGroup* subGroup = dynamic_cast<const VariableGroup*>(child.second);
        if (subGroup != nullptr)
            subGroup->SaveValues(values, path + "/");
        else if (dynamic_cast<const CallbackTrigger*>(child.second) == nullptr)
            values[path] = child.second->ToString();
    }
}

void VariableGroup::LoadValues( const EditorSettings::Values& values, const string& pathPrefix )
{
    if (m_Section != nullptr)
    {
        // key는 path 순서로 정렬되어 있으므로 pathPrefix로 시작하는 key는 lower_bound부터 이어진다. 그중 '/'가 더 있는 key는 아래 group의 것이다.
        TuningSection::Values sectionValues;
        for (auto value = values.lower_bound(pathPrefix); value != values.end() && value->first.compare(0, pathPrefix.size(), pathPrefix) == 0; ++value)
        {
            if (value->first.find('/', pathPrefix.size()) == string::npos)
                sectionValues[value->first.substr(pathPrefix.size())] = value->second;
        }
        m_Section->Load(sectionValues);
    }

    for (const auto& child : m_Children)
    {
        const string path = pathPrefix + child.first;
        VariableGroup* subGroup = dynamic_cast<VariableGroup*>(child.second);
        if (subGroup != nullptr)
        {
            subGroup->LoadValues(values, path + "/");
            continue;
        }

        const auto value = values.find(path);
        if (value != values.end())
            child.second->SetFromString(value->second);
    }
}

//=====================================================================================================================
// EngineVar implementations

EngineVar::EngineVar( void ) : m_GroupPtr(nullptr)
{
}

EngineVar::EngineVar( const std::string& path, ActionCallback pfnCallback) : m_GroupPtr(nullptr), m_ActionCallback(pfnCallback)
{
    EngineTuning::RegisterVariable(path, *this);
}

namespace
{
    // ImGui는 format의 자릿수로 값을 반올림하므로 step을 표현할 만큼 자릿수가 있어야 한다.
    std::string FloatFormat( float stepSize )
    {
        int decimals = 1;
        while (decimals < 6)
        {
            const float scaled = stepSize * powf(10.0f, (float)decimals);
            if (fabsf(scaled - roundf(scaled)) < 1e-3f)
                break;
            ++decimals;
        }
        return "%." + std::to_string(decimals) + "f";
    }

    constexpr float kDragPixelsPerStep = 10.0f;

    float DragSpeed( float stepSize )
    {
        return stepSize / kDragPixelsPerStep;
    }
}

BoolVar::BoolVar( const std::string& path, bool val, ActionCallback pfnCallback )
    : EngineVar(path, pfnCallback)
{
    m_Flag = val;
}

void BoolVar::Edit( const char* label )
{
    bool flag = m_Flag;
    if (ImGui::Checkbox(label, &flag))
        flag ? Increment() : Decrement();
}

std::string BoolVar::ToString( void ) const
{
    return m_Flag ? "on" : "off";
} 

void BoolVar::SetFromString( const std::string& text )
{
    const bool flag = text == "on";
    if (flag != m_Flag)
        flag ? Increment() : Decrement();
}

NumVar::NumVar( const std::string& path, float val, float minVal, float maxVal, float stepSize, ActionCallback pfnCallback )
    : EngineVar(path, pfnCallback)
{
    ASSERT(minVal <= maxVal);
    m_MinValue = minVal;
    m_MaxValue = maxVal;
    m_Value = Clamp(val);
    m_StepSize = stepSize;
}

void NumVar::Edit( const char* label )
{
    float value = m_Value;
    if (!ImGui::DragFloat(label, &value, DragSpeed(m_StepSize), m_MinValue, m_MaxValue, FloatFormat(m_StepSize).c_str(),
        ImGuiSliderFlags_AlwaysClamp))
        return;

    const float oldValue = m_Value;
    m_Value = Clamp(value);
    m_Value > oldValue ? EngineVar::Increment() : EngineVar::Decrement();
}

std::string NumVar::ToString( void ) const
{
    // float로 다시 읽었을 때 같은 값이 되는 자릿수로 쓴다.
    char buf[128];
    sprintf_s(buf, "%.9g", m_Value);
    return buf;
}

void NumVar::SetFromString( const std::string& text )
{
    float value;
    if (sscanf_s(text.c_str(), "%f", &value) != 1)
        return;

    const float oldValue = m_Value;
    m_Value = Clamp(value);
    if (m_Value != oldValue)
        m_Value > oldValue ? EngineVar::Increment() : EngineVar::Decrement();
}

#if _MSC_VER < 1800
__forceinline float log2( float x ) { return log(x) / log(2.0f); }
__forceinline float exp2( float x ) { return pow(2.0f, x); }
#endif

ExpVar::ExpVar( const std::string& path, float val, float minExp, float maxExp, float expStepSize, ActionCallback pfnCallback )
    : NumVar(path, log2f(val), minExp, maxExp, expStepSize, pfnCallback)
{
}

ExpVar& ExpVar::operator=( float val )
{
    m_Value = Clamp(log2f(val));
    return *this;
}

ExpVar::operator float() const
{
    return exp2f(m_Value);
}

void ExpVar::Edit( const char* label )
{
    // 지수를 끌되 실제 값을 보여 주려고, %가 없으면 그대로 표시되는 ImGui format에 실제 값을 넣는다.
    // 직접 입력한 값은 지수로 해석되므로 입력은 막는다.
    char display[64];
    sprintf_s(display, "%g", (float)*this);

    float exponent = m_Value;
    if (!ImGui::DragFloat(label, &exponent, DragSpeed(m_StepSize), m_MinValue, m_MaxValue, display,
        ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput))
        return;

    const float oldExponent = m_Value;
    m_Value = Clamp(exponent);
    m_Value > oldExponent ? EngineVar::Increment() : EngineVar::Decrement();
}

std::string ExpVar::ToString( void ) const
{
    char buf[128];
    sprintf_s(buf, "%.9g", (float)*this);
    return buf;
}

void ExpVar::SetFromString( const std::string& text )
{
    float value;
    if (sscanf_s(text.c_str(), "%f", &value) != 1)
        return;

    const float oldExponent = m_Value;
    *this = value;
    if (m_Value != oldExponent)
        m_Value > oldExponent ? EngineVar::Increment() : EngineVar::Decrement();
}

IntVar::IntVar( const std::string& path, int32_t val, int32_t minVal, int32_t maxVal, int32_t stepSize, ActionCallback pfnCallback )
    : EngineVar(path, pfnCallback)
{
    ASSERT(minVal <= maxVal);
    m_MinValue = minVal;
    m_MaxValue = maxVal;
    m_Value = Clamp(val);
    m_StepSize = stepSize;
}

void IntVar::Edit( const char* label )
{
    int value = m_Value;
    if (!ImGui::DragInt(label, &value, DragSpeed((float)m_StepSize), m_MinValue, m_MaxValue, "%d",
        ImGuiSliderFlags_AlwaysClamp))
        return;

    const int32_t oldValue = m_Value;
    m_Value = Clamp(value);
    m_Value > oldValue ? EngineVar::Increment() : EngineVar::Decrement();
}

std::string IntVar::ToString( void ) const
{
    char buf[128];
    sprintf_s(buf, "%d", m_Value);
    return buf;
} 

void IntVar::SetFromString( const std::string& text )
{
    int32_t value;
    if (sscanf_s(text.c_str(), "%d", &value) != 1)
        return;

    const int32_t oldValue = m_Value;
    m_Value = Clamp(value);
    if (m_Value != oldValue)
        m_Value > oldValue ? EngineVar::Increment() : EngineVar::Decrement();
}


EnumVar::EnumVar( const std::string& path, int32_t initialVal, int32_t listLength, const char** listLabels, ActionCallback pfnCallback )
    : EngineVar(path, pfnCallback)
{
    ASSERT(listLength > 0);
    m_EnumLength = listLength;
    m_EnumLabels = listLabels;
    m_Value = Clamp(initialVal);
}

void EnumVar::Edit( const char* label )
{
    int value = m_Value;
    if (!ImGui::Combo(label, &value, m_EnumLabels, m_EnumLength))
        return;

    m_Value = Clamp(value);
    EngineVar::Bang();
}

std::string EnumVar::ToString( void ) const
{
    return m_EnumLabels[m_Value];
} 

// 순서가 아닌 label로 찾으므로 항목의 순서가 바뀌어도 같은 항목을 고른다.
void EnumVar::SetFromString( const std::string& text )
{
    for (int32_t i = 0; i < m_EnumLength; ++i)
    {
        if (text != m_EnumLabels[i])
            continue;

        if (i != m_Value)
        {
            m_Value = i;
            EngineVar::Bang();
        }
        return;
    }
}

DynamicEnumVar::DynamicEnumVar( const std::string& path, ActionCallback pfnCallback )
    : EngineVar(path, pfnCallback)
{
    m_EnumCount = 0;
    m_Value = 0;
}

void DynamicEnumVar::Edit( const char* label )
{
    if (m_EnumCount == 0)
        return;

    if (!ImGui::BeginCombo(label, Utility::WideStringToUTF8(m_EnumLabels[m_Value]).c_str()))
        return;

    for (int32_t i = 0; i < m_EnumCount; ++i)
    {
        const bool isSelected = (i == m_Value);
        if (ImGui::Selectable(Utility::WideStringToUTF8(m_EnumLabels[i]).c_str(), isSelected) && !isSelected)
        {
            m_Value = i;
            EngineVar::Bang();
        }
    }
    ImGui::EndCombo();
}

std::string DynamicEnumVar::ToString( void ) const
{
    return Utility::WideStringToUTF8(m_EnumLabels[m_Value]);
} 

void DynamicEnumVar::SetFromString( const std::string& text )
{
    for (int32_t i = 0; i < m_EnumCount; ++i)
    {
        if (text != Utility::WideStringToUTF8(m_EnumLabels[i]))
            continue;

        if (i != m_Value)
        {
            m_Value = i;
            EngineVar::Bang();
        }
        return;
    }
}


CallbackTrigger::CallbackTrigger( const std::string& path, std::function<void (void*)> callback, void* args )
    : EngineVar(path)
{
    m_Callback = callback;
    m_Arguments = args;
}

void CallbackTrigger::Edit( const char* label )
{
    if (ImGui::Button(label))
        Bang();
}

// EngineVar의 생성자로 등록하지 않는다. Initialize 뒤에 만들면 등록이 바로 AddToVariableGraph까지 가는데, EngineVar의 생성자 안에서는
// 이것이 아직 TuningSection이 아니다.
TuningSection::TuningSection( const std::string& path, std::function<void (void)> show,
    std::function<void (Values&)> save, std::function<void (const Values&)> load )
    : m_Show(show), m_Save(save), m_Load(load)
{
    EngineTuning::RegisterVariable(path, *this);
}

void TuningSection::Save( Values& values ) const
{
    if (m_Save)
        m_Save(values);
}

void TuningSection::Load( const Values& values ) const
{
    if (m_Load)
        m_Load(values);
}

//=====================================================================================================================
// EngineTuning namespace methods

void EngineTuning::Initialize( void )
{

    for (int32_t i = 0; i < s_UnregisteredCount; ++i)
    {
        ASSERT(strlen(s_UnregisteredPath[i]) > 0, "Register = %d\n", i);
        ASSERT(s_UnregisteredVariable[i] != nullptr);
        AddToVariableGraph(s_UnregisteredPath[i], *s_UnregisteredVariable[i]);
    }
    s_UnregisteredCount = -1;

    EditorSettings::Register("Engine Tuning",
        [](EditorSettings::Values& values) { VariableGroup::sm_RootGroup.SaveValues(values, ""); },
        [](const EditorSettings::Values& values) { VariableGroup::sm_RootGroup.LoadValues(values, ""); });
}

void EngineTuning::Update( void )
{
    ImGuiLayer::ShowPanel("Engine Tuning", s_ShowPanel, []
    {
        VariableGroup::sm_RootGroup.ShowChildren();
    });
}

void EngineTuning::AddToVariableGraph( const string& path, EngineVar& var )
{
    vector<string> separatedPath;
    string leafName;
    size_t start = 0, end = 0;

    while (1)
    {
        end = path.find('/', start);
        if (end == string::npos)
        {
            leafName = path.substr(start);
            break;
        }
        else
        {
            separatedPath.push_back(path.substr(start, end - start));
            start = end + 1;
        }
    }

    // section은 path의 group에 붙는다. 그 group은 변수의 group처럼 없으면 만든다.
    const TuningSection* section = dynamic_cast<const TuningSection*>(&var);
    if (section != nullptr)
        separatedPath.push_back(leafName);

    VariableGroup* group = &VariableGroup::sm_RootGroup;

    for (auto iter = separatedPath.begin(); iter != separatedPath.end(); ++iter )
    {
        VariableGroup* nextGroup;
        EngineVar* node = group->FindChild(*iter);
        if (node == nullptr)
        {
            nextGroup = new VariableGroup();
            group->AddChild(*iter, *nextGroup);
            group = nextGroup;
        }
        else
        {
            nextGroup = dynamic_cast<VariableGroup*>(node);
            ASSERT(nextGroup != nullptr, "Attempted to trash the tweak graph");
            group = nextGroup;
        }
    }

    if (section != nullptr)
        group->SetSection(*section);
    else
        group->AddChild(leafName, var);
}

void EngineTuning::RegisterVariable( const std::string& path, EngineVar& var )
{
    if (s_UnregisteredCount >= 0)
    {
        int32_t Idx = s_UnregisteredCount++;
        strcpy_s(s_UnregisteredPath[Idx], path.c_str());
        s_UnregisteredVariable[Idx] = &var;
    }
    else
    {
        AddToVariableGraph( path, var );
    }
}
