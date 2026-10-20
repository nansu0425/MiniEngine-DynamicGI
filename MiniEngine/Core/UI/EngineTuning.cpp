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

    void ShowChildren( void );

    void SaveToFile( FILE* file, int fileMargin );
    void LoadSettingsFromFile( FILE* file );

    virtual void SetValue( FILE*, const std::string& ) override {}

    static VariableGroup sm_RootGroup;

private:
    std::map<string, EngineVar*> m_Children;
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
                subGroup->ShowChildren();
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

void VariableGroup::SaveToFile( FILE* file, int fileMargin )
{
    for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
    {
        const char* buffer = (iter->first).c_str();

        VariableGroup* subGroup = dynamic_cast<VariableGroup*>(iter->second);
        if (subGroup != nullptr)
        {		
            fprintf(file, "%*c + %s ...\r\n", fileMargin, ' ', buffer);
            subGroup->SaveToFile(file, fileMargin + 5);
        }
        else if (dynamic_cast<CallbackTrigger*>(iter->second) == nullptr)
        {
            fprintf(file, "%*c %s:  %s\r\n", fileMargin, ' ', buffer, iter->second->ToString().c_str());
        }		
    }
}

void VariableGroup::LoadSettingsFromFile( FILE* file )
{
    for (auto iter = m_Children.begin(); iter != m_Children.end(); ++iter)
    {
        VariableGroup* subGroup = dynamic_cast<VariableGroup*>(iter->second);
        if (subGroup != nullptr)
        {
            char skippedLines[100];
            fscanf_s(file, "%*s %[^\n]", skippedLines, (int)_countof(skippedLines));
            subGroup->LoadSettingsFromFile(file);
        }
        else
        {	
            iter->second->SetValue(file, iter->first);
        }
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

void BoolVar::SetValue(FILE* file, const std::string& setting)
{	
    std::string pattern = "\n " + setting + ": %s";
    char valstr[6];

    // Search through the file for an entry that matches this setting's name
    fscanf_s(file, pattern.c_str(), valstr, _countof(valstr));

    // Look for one of the many affirmations
    m_Flag = (
        0 == _stricmp(valstr, "1") ||
        0 == _stricmp(valstr, "on") ||
        0 == _stricmp(valstr, "yes") ||
        0 == _stricmp(valstr, "true") );
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
    char buf[128];
    sprintf_s(buf, "%f", m_Value);
    return buf;
} 

void NumVar::SetValue(FILE* file, const std::string& setting) 
{
    std::string scanString = "\n" + setting + ": %f";
    float valueRead;
    
    //If we haven't read correctly, just keep m_Value at default value
    if (fscanf_s(file, scanString.c_str(), &valueRead))
        *this = valueRead; 
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
    sprintf_s(buf, "%f", (float)*this);
    return buf;
} 

void ExpVar::SetValue(FILE* file, const std::string& setting) 
{
    std::string scanString = "\n" + setting + ": %f";
    float valueRead;
    
    //If we haven't read correctly, just keep m_Value at default value
    if (fscanf_s(file, scanString.c_str(), &valueRead))
        *this = valueRead;
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

void IntVar::SetValue(FILE* file, const std::string& setting) 
{
    std::string scanString = "\n" + setting + ": %d";
    int32_t valueRead;
    
    if (fscanf_s(file, scanString.c_str(), &valueRead))
        *this = valueRead;
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

void EnumVar::SetValue(FILE* file, const std::string& setting) 
{
    std::string scanString = "\n" + setting + ": %[^\n]";
    char valueRead[14];
        
    if (fscanf_s(file, scanString.c_str(), valueRead, _countof(valueRead)) == 1)
    {
        std::string valueReadStr = valueRead;
        valueReadStr = valueReadStr.substr(0, valueReadStr.length() - 1);

        //if we don't find the string, then leave m_EnumLabes[m_Value] as default
        for(int32_t i = 0; i < m_EnumLength; ++i)
        {
            if (m_EnumLabels[i] == valueReadStr)
            {
                m_Value = i;
                break;
            }
        }
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

void DynamicEnumVar::SetValue(FILE* file, const std::string& setting) 
{
    std::string scanString = "\n" + setting + ": %[^\n]";
    char valueRead[14];

    if (fscanf_s(file, scanString.c_str(), valueRead, _countof(valueRead)) == 1)
    {
        std::string valueReadStr = valueRead;
        valueReadStr = valueReadStr.substr(0, valueReadStr.length() - 1);

        std::wstring wvalue = Utility::UTF8ToWideString(valueReadStr);

        //if we don't find the string, then leave m_EnumLabes[m_Value] as default
        for (int32_t i = 0; i < m_EnumCount; ++i)
        {
            if (m_EnumLabels[i] == wvalue)
            {
                m_Value = i;
                break;
            }
        }
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

void CallbackTrigger::SetValue(FILE* file, const std::string& setting) 
{
    //Skip over setting without reading anything
    std::string scanString = "\n" + setting + ": %[^\n]";
    char skippedLines[100];
    fscanf_s(file, scanString.c_str(), skippedLines, _countof(skippedLines));
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

}

void EngineTuning::Update( void )
{
    ImGuiLayer::ShowPanel("Engine Tuning", s_ShowPanel, []
    {
        VariableGroup::sm_RootGroup.ShowChildren();
    });
}

/*
void StartSave(void*)
{
    FILE* settingsFile;
    fopen_s(&settingsFile, "engineTuning.txt", "wb");
    if (settingsFile != nullptr)
    {
        VariableGroup::sm_RootGroup.SaveToFile(settingsFile, 2 );
        fclose(settingsFile);
    }
}
std::function<void(void*)> StartSaveFunc = StartSave;
static CallbackTrigger Save("Save Settings", StartSaveFunc, nullptr); 

void StartLoad(void*)
{
    FILE* settingsFile;
    fopen_s(&settingsFile, "engineTuning.txt", "rb");
    if (settingsFile != nullptr)
    {
        VariableGroup::sm_RootGroup.LoadSettingsFromFile(settingsFile);
        fclose(settingsFile);
    }
}
std::function<void(void*)> StartLoadFunc = StartLoad;
static CallbackTrigger Load("Load Settings", StartLoadFunc, nullptr); 
*/

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
