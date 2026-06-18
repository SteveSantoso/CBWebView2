// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2D3D12RHIBridge.h"

// Must be the first D3D-related include in this translation unit so the AgilitySDK headers win
// the include-guard race against the Windows SDK ones (see header comment).
#include "ID3D12DynamicRHI.h"

namespace CBWebView2D3D12Bridge
{
	ID3D12Device* GetDevice()
	{
		return IsRHID3D12() ? GetID3D12DynamicRHI()->RHIGetDevice(0) : nullptr;
	}

	FTextureRHIRef CreateTexture2DFromResource(EPixelFormat Format, ID3D12Resource* Resource)
	{
		if (!IsRHID3D12() || !Resource)
		{
			return nullptr;
		}

		return GetID3D12DynamicRHI()->RHICreateTexture2DFromResource(
			Format,
			TexCreate_ShaderResource,
			FClearValueBinding::None,
			Resource);
	}

	void WaitManualFence(FRHICommandList& RHICmdList, ID3D12Fence* Fence, uint64 Value)
	{
		if (IsRHID3D12() && Fence)
		{
			GetID3D12DynamicRHI()->RHIWaitManualFence(RHICmdList, Fence, Value);
		}
	}
}
