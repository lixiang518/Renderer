#pragma once

#include "SceneRendering.h"

class RuntimeRender : public FSceneRenderer
{
public:
	RuntimeRender(const FSceneViewFamily* InViewFamily, FHitProxyConsumer* HitProxyConsumer);

	// FSceneRenderer interface
	virtual void Render(FRDGBuilder& GraphBuilder, const FSceneRenderUpdateInputs* SceneUpdateInputs) override;
};