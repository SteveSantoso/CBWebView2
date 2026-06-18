// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"
#include "RHIFwd.h"

class FRHICommandList;
struct ID3D12Device;
struct ID3D12Fence;
struct ID3D12Resource;

/**
 * Narrow bridge around ID3D12DynamicRHI.
 *
 * ID3D12DynamicRHI.h drags in the AgilitySDK d3dx12 headers, which must be compiled before any
 * Windows SDK d3d header locks the d3d12/d3dcommon include guards to the older SDK versions.
 * The WebView2 translation units include Windows SDK d3d headers early and would poison that include
 * order, so the three D3D12RHI calls the plugin needs live in their own clean translation unit.
 */
namespace CBWebView2D3D12Bridge
{
	/** Unreal D3D12 device for node 0, or null when the active RHI is not D3D12. */
	ID3D12Device* GetDevice();

	/** Wrap an externally created (shared) D3D12 resource as an RHI texture usable as a copy source. */
	FTextureRHIRef CreateTexture2DFromResource(EPixelFormat Format, ID3D12Resource* Resource);

	/** Record a GPU-side wait on the graphics queue at this point in the command list. */
	void WaitManualFence(FRHICommandList& RHICmdList, ID3D12Fence* Fence, uint64 Value);
}
