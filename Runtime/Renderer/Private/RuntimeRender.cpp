#include "RuntimeRender.h"

#include "RuntimeDrawTrianglePass.h"
#include "SceneRendering.h"

/*-----------------------------------------------------------------------------
	RuntimeRender
-----------------------------------------------------------------------------*/
RuntimeRender::RuntimeRender(const FSceneViewFamily* InViewFamily, FHitProxyConsumer* HitProxyConsumer)
	: FSceneRenderer(InViewFamily, HitProxyConsumer)
{
}


void RuntimeRender::Render(FRDGBuilder& GraphBuilder, const FSceneRenderUpdateInputs* SceneUpdateInputs)
{
	FRDGTextureRef ViewFamilyTexture = TryCreateViewFamilyTexture(GraphBuilder, ViewFamily);
	
	AddRuntimeDrawTrianglePass(GraphBuilder, ViewFamilyTexture);
}
