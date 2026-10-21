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

#include "Core/pch.h"

class GraphicsContext;

namespace GameCore
{
    extern bool gIsSupending;

    class IGameApp
    {
    public:
        // This function can be used to initialize application state and will run after essential
        // hardware resources are allocated.  Some state that does not depend on these resources
        // should still be initialized in the constructor such as pointers and flags.
        virtual void Startup( void ) = 0;
        virtual void Cleanup( void ) = 0;

        // Decide if you want the app to exit.  By default, app continues until the 'ESC' key is pressed.
        virtual bool IsDone( void );

        // The update method will be invoked once per frame.  Both state updating and scene
        // rendering should be handled by this method.
        virtual void Update( float deltaT ) = 0;

        // Official rendering pass
        virtual void RenderScene( void ) = 0;

        // tone mapping 같은 post effect를 거치면 안 되는 LDR 그림을 scene 위에 덮어 그린다.
        // 지워진 scene overlay가 render target으로 묶여 있다.
        virtual void RenderSceneOverlay( GraphicsContext& ) {};

        // Inline raytracing을 쓰는 app은 override해서 이를 지원하는 device를 요구한다.
        virtual bool RequiresInlineRaytracing() const { return false; }
    };
}

namespace GameCore
{
    int RunApplication( IGameApp& app, const wchar_t* className, HINSTANCE hInst, int nCmdShow );
}

#define CREATE_APPLICATION( app_class ) \
    int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPWSTR /*lpCmdLine*/, _In_ int nCmdShow) \
    { \
        return GameCore::RunApplication( app_class(), L#app_class, hInstance, nCmdShow ); \
    }
