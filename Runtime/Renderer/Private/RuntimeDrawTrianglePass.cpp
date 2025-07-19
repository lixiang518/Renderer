
#include "RuntimeDrawTrianglePass.h"

// 顶点着色器
class FSimpleVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSimpleVS);
	SHADER_USE_PARAMETER_STRUCT(FSimpleVS, FGlobalShader)
	
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, VertexBuffer)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

// 像素着色器
class FSimplePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSimplePS);
	SHADER_USE_PARAMETER_STRUCT(FSimplePS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSimpleVS, "/Engine/Private/RuntimeDrawTriangleShader/RuntimeDrawTriangleShader.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FSimplePS, "/Engine/Private/RuntimeDrawTriangleShader/RuntimeDrawTriangleShader.usf", "MainPS", SF_Pixel);

void AddRuntimeDrawTrianglePass(FRDGBuilder& GraphBuilder, FRDGTextureRef ViewFamilyTexture)
{
	// 准备顶点数据
	TArray Vertices = {
		FVector3f(0.0f, 0.25f, 0.0f),  
		FVector3f(0.25f, -0.25f, 0.0f), 
		FVector3f(-0.25f, -0.25f, 0.0f) 
	};
	const uint32 DataSize = Vertices.Num() * sizeof(FVector3f);

	// 创建RDG顶点缓冲区
	FRDGBufferRef VertexBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector3f), Vertices.Num()),
		TEXT("TriangleVB"));
	GraphBuilder.QueueBufferUpload(VertexBuffer, Vertices.GetData(), DataSize, ERDGInitialDataFlags::None);

	// 渲染目标绑定游戏视口（viewport）的后缓冲区（backBuffer）
	auto *PassParameters = GraphBuilder.AllocParameters<FSimpleVS::FParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(ViewFamilyTexture, ERenderTargetLoadAction::EClear);
	
	// 添加RDG绘制Pass
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("RuntimeDrawTrianglePass"), 
		PassParameters,
		ERDGPassFlags::Raster, 
		[VertexBuffer](FRHICommandList &RHICmdList)
		{
			// 创建顶点声明（仅包含位置）
			FVertexDeclarationElementList Elements;
			Elements.Add(FVertexElement(0, 0, VET_Float3, 0, sizeof(FVector)));
			
			// 设置图形管线状态
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			// 绑定Shader
			TShaderMapRef<FSimpleVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			TShaderMapRef<FSimplePS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			FVertexDeclarationRHIRef VertexDecl = RHICreateVertexDeclaration(Elements);
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertexDecl;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			// 提交PSO
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 1);

			// 绑定顶点流
			RHICmdList.SetStreamSource(0, VertexBuffer->GetRHI(), 0);

			// 提交绘制命令
			RHICmdList.DrawPrimitive(0, 1, 1); 
		});
	
}