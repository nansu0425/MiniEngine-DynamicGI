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

#pragma once

#include <string>
#include <stdint.h>
#include <float.h>
#include <map>
#include <set>

class VariableGroup;

class EngineVar
{
public:

    virtual ~EngineVar() {}

    virtual void Increment( void ) { m_ActionCallback(ActionType::Increment); }
    virtual void Decrement( void ) { m_ActionCallback(ActionType::Decrement); }
    virtual void Bang( void ) { m_ActionCallback(ActionType::Bang); }

    // Edit과 SetFromString의 구현은 값을 바꿨으면 Increment, Decrement, Bang 중 하나를 거쳐 action callback을 불러야 한다.
    virtual void Edit( const char* /*label*/ ) {}
    // ToString이 만든 문자열을 SetFromString이 읽어 editor.ini에 저장한 값을 되돌린다.
    virtual std::string ToString( void ) const { return ""; }
    virtual void SetFromString( const std::string& text ) = 0;

    enum class ActionType
    {
        Increment,
        Decrement,
        Bang
    };

    typedef std::function<void(ActionType)> ActionCallback;

protected:

    EngineVar( void );
    EngineVar( const std::string& path, ActionCallback pfnCallback = DefaultActionHandler );

    static void DefaultActionHandler(ActionType)
    {
        // nothing
    }

private:
    friend class VariableGroup;
    VariableGroup* m_GroupPtr;

    ActionCallback m_ActionCallback;
};

class BoolVar : public EngineVar
{
public:
    BoolVar( const std::string& path, bool val, ActionCallback pfnCallback = EngineVar::DefaultActionHandler );
    BoolVar& operator=( bool val ) { m_Flag = val; return *this; }
    operator bool() const { return m_Flag; }

    virtual void Increment( void ) override { m_Flag = true; EngineVar::Increment(); }
    virtual void Decrement( void ) override { m_Flag = false; EngineVar::Decrement(); }
    virtual void Bang( void ) override { m_Flag = !m_Flag;  EngineVar::Bang(); }

    virtual void Edit( const char* label ) override;
    virtual std::string ToString( void ) const override;
    virtual void SetFromString( const std::string& text ) override;

private:
    bool m_Flag;
};

class NumVar : public EngineVar
{
public:
    NumVar( const std::string& path, float val, float minValue = -FLT_MAX, float maxValue = FLT_MAX, float stepSize = 1.0f, ActionCallback pfnCallback = EngineVar::DefaultActionHandler);
    NumVar& operator=( float val ) { m_Value = Clamp(val); return *this; }
    operator float() const { return m_Value; }

    virtual void Increment( void ) override { m_Value = Clamp(m_Value + m_StepSize); EngineVar::Increment(); }
    virtual void Decrement( void ) override { m_Value = Clamp(m_Value - m_StepSize); EngineVar::Decrement(); }

    virtual void Edit( const char* label ) override;
    virtual std::string ToString( void ) const override;
    virtual void SetFromString( const std::string& text ) override;

protected:
    float Clamp( float val ) { return val > m_MaxValue ? m_MaxValue : val < m_MinValue ? m_MinValue : val; }

    float m_Value;
    float m_MinValue;
    float m_MaxValue;
    float m_StepSize;
};

class ExpVar : public NumVar
{
public:
    ExpVar( const std::string& path, float val, float minExp = -FLT_MAX, float maxExp = FLT_MAX, float expStepSize = 1.0f, ActionCallback pfnCallback = EngineVar::DefaultActionHandler);
    ExpVar& operator=( float val );	// m_Value = log2(val)
    operator float() const;			// returns exp2(m_Value)

    virtual void Edit( const char* label ) override;
    virtual std::string ToString( void ) const override;
    virtual void SetFromString( const std::string& text ) override;

};

class IntVar : public EngineVar
{
public:
    IntVar( const std::string& path, int32_t val, int32_t minValue = 0, int32_t maxValue = (1 << 24) - 1, int32_t stepSize = 1, ActionCallback pfnCallback = EngineVar::DefaultActionHandler);
    IntVar& operator=( int32_t val ) { m_Value = Clamp(val); return *this; }
    operator int32_t() const { return m_Value; }

    virtual void Increment( void ) override { m_Value = Clamp(m_Value + m_StepSize); EngineVar::Increment(); }
    virtual void Decrement( void ) override { m_Value = Clamp(m_Value - m_StepSize); EngineVar::Decrement(); }

    virtual void Edit( const char* label ) override;
    virtual std::string ToString( void ) const override;
    virtual void SetFromString( const std::string& text ) override;

protected:
    int32_t Clamp( int32_t val ) { return val > m_MaxValue ? m_MaxValue : val < m_MinValue ? m_MinValue : val; }

    int32_t m_Value;
    int32_t m_MinValue;
    int32_t m_MaxValue;
    int32_t m_StepSize;
};

class EnumVar : public EngineVar
{
public:
    EnumVar( const std::string& path, int32_t initialVal, int32_t listLength, const char** listLabels, ActionCallback pfnCallback = EngineVar::DefaultActionHandler);
    EnumVar& operator=( int32_t val ) { m_Value = Clamp(val); return *this; }
    operator int32_t() const { return m_Value; }

    virtual void Increment( void ) override { m_Value = (m_Value + 1) % m_EnumLength; EngineVar::Increment(); }
    virtual void Decrement( void ) override { m_Value = (m_Value + m_EnumLength - 1) % m_EnumLength; EngineVar::Decrement(); }

    virtual void Edit( const char* label ) override;
    virtual std::string ToString( void ) const override;
    virtual void SetFromString( const std::string& text ) override;

    void SetListLength(int32_t listLength) { m_EnumLength = listLength; m_Value = Clamp(m_Value); }

private:
    int32_t Clamp( int32_t val ) { return val < 0 ? 0 : val >= m_EnumLength ? m_EnumLength - 1 : val; }

    int32_t m_Value;
    int32_t m_EnumLength;
    const char** m_EnumLabels;
};

class DynamicEnumVar : public EngineVar
{
public:
    DynamicEnumVar( const std::string& path, ActionCallback pfnCallback = EngineVar::DefaultActionHandler);
    DynamicEnumVar& operator=( int32_t val ) { m_Value = Clamp(val); return *this; }
    operator int32_t() const { return m_Value; }

    virtual void Increment( void ) override { m_Value = (m_Value + 1) % m_EnumCount; EngineVar::Increment(); }
    virtual void Decrement( void ) override { m_Value = (m_Value + m_EnumCount - 1) % m_EnumCount; EngineVar::Decrement(); }

    virtual void Edit( const char* label ) override;
    virtual std::string ToString( void ) const override;
    virtual void SetFromString( const std::string& text ) override;

    void AddEnum(const std::wstring& enumLabel) { m_EnumLabels.push_back(enumLabel); m_EnumCount++; }

private:
    int32_t Clamp( int32_t val ) { return val < 0 ? 0 : val >= m_EnumCount ? m_EnumCount - 1 : val; }

    int32_t m_Value;
    int32_t m_EnumCount;
    std::vector<std::wstring> m_EnumLabels;
};

class CallbackTrigger : public EngineVar
{
public:
    CallbackTrigger( const std::string& path, std::function<void (void*)> callback, void* args = nullptr );

    virtual void Bang( void ) override { m_Callback(m_Arguments); }

    virtual void Edit( const char* label ) override;
    // 값이 없으므로 저장하지 않는다.
    virtual void SetFromString( const std::string& ) override {}

private:
    std::function<void (void*)> m_Callback;
    void* m_Arguments;
};

// EngineVar의 widget 하나로 담을 수 없는 engine 설정과 그 설정을 쓰는 도구를 path의 group에 그린다. group을 펼치면 show가 그린 것이
// group 아래의 변수보다 먼저 보인다. save가 채운 값은 key 앞에 path를 붙여 다른 변수와 함께 저장하고, load는 그중 이 section의 값만 받는다.
// key에는 '/'를 쓰지 않고 group 아래의 변수와 같은 이름을 쓰지 않는다.
class TuningSection : public EngineVar
{
public:
    typedef std::map<std::string, std::string> Values;

    TuningSection( const std::string& path, std::function<void (void)> show,
        std::function<void (Values&)> save = nullptr, std::function<void (const Values&)> load = nullptr );

    void Show( void ) const { m_Show(); }
    void Save( Values& values ) const;
    void Load( const Values& values ) const;

    // 값은 Save와 Load로 저장한다.
    virtual void SetFromString( const std::string& ) override {}

private:
    std::function<void (void)> m_Show;
    std::function<void (Values&)> m_Save;
    std::function<void (const Values&)> m_Load;
};

namespace EngineTuning
{
    void Initialize( void );
    void Update( void );

} // namespace EngineTuning
