#include "TEMPLATE_NAME/pch.h"
#include "Core/App/GameCore.h"
#include "Core/Graphics/GraphicsCore.h"
#include "Core/App/SystemTime.h"
#include "Core/UI/TextRenderer.h"
#include "Core/App/GameInput.h"
#include "Core/Graphics/Command/CommandContext.h"
#include "Core/Graphics/Pipeline/RootSignature.h"
#include "Core/Graphics/Pipeline/PipelineState.h"
#include "Core/Graphics/Resource/BufferManager.h"

using namespace GameCore;
using namespace Graphics;

class TEMPLATE_NAME : public GameCore::IGameApp
{
public:

    TEMPLATE_NAME()
    {
    }

    virtual void Startup( void ) override;
    virtual void Cleanup( void ) override;

    virtual void Update( float deltaT ) override;
    virtual void RenderScene( void ) override;

private:
};

CREATE_APPLICATION( TEMPLATE_NAME )

void TEMPLATE_NAME::Startup( void )
{
    // Setup your data
}

void TEMPLATE_NAME::Cleanup( void )
{
    // Free up resources in an orderly fashion
}

void TEMPLATE_NAME::Update( float /*deltaT*/ )
{
    ScopedTimer _prof(L"Update State");

    // Update something
}

void TEMPLATE_NAME::RenderScene( void )
{
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.ClearColor(g_SceneColorBuffer);
    gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV());
    gfxContext.SetViewportAndScissor(0, 0, g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight());

    // Rendering something

    gfxContext.Finish();
}
