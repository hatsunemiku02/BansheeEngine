//__________________________ Banshee Project - A modern game development toolkit _________________________________//
//_____________________________________ www.banshee-project.com __________________________________________________//
//________________________ Copyright (c) 2014 Marko Pintera. All rights reserved. ________________________________//
#include "BsRenderSystemCapabilities.h"
#include "BsException.h"
#include "BsString.h"

namespace BansheeEngine 
{
	Vector<BansheeEngine::String> RenderSystemCapabilities::msGPUVendorStrings;

	RenderSystemCapabilities::RenderSystemCapabilities()
	{
		for(int i = 0; i < CAPS_CATEGORY_COUNT; i++)
		{
			mCapabilities[i] = 0;
		}

		mNumTextureUnitsPerStage[GPT_VERTEX_PROGRAM] = 0;
		mNumTextureUnitsPerStage[GPT_FRAGMENT_PROGRAM] = 0;
		mNumTextureUnitsPerStage[GPT_GEOMETRY_PROGRAM] = 0;
		mNumTextureUnitsPerStage[GPT_HULL_PROGRAM] = 0;
		mNumTextureUnitsPerStage[GPT_DOMAIN_PROGRAM] = 0;
		mNumTextureUnitsPerStage[GPT_COMPUTE_PROGRAM] = 0;

		mNumGpuParamBlocksPerStage[GPT_VERTEX_PROGRAM] = 0;
		mNumGpuParamBlocksPerStage[GPT_FRAGMENT_PROGRAM] = 0;
		mNumGpuParamBlocksPerStage[GPT_GEOMETRY_PROGRAM] = 0;
		mNumGpuParamBlocksPerStage[GPT_HULL_PROGRAM] = 0;
		mNumGpuParamBlocksPerStage[GPT_DOMAIN_PROGRAM] = 0;
		mNumGpuParamBlocksPerStage[GPT_COMPUTE_PROGRAM] = 0;
	}

	RenderSystemCapabilities::~RenderSystemCapabilities()
	{
	}

	GPUVendor RenderSystemCapabilities::vendorFromString(const String& vendorString)
	{
		initVendorStrings();
		GPUVendor ret = GPU_UNKNOWN;
		String cmpString = vendorString;
		StringUtil::toLowerCase(cmpString);
		for (int i = 0; i < GPU_VENDOR_COUNT; ++i)
		{
			// case insensitive (lower case)
			if (msGPUVendorStrings[i] == cmpString)
			{
				ret = static_cast<GPUVendor>(i);
				break;
			}
		}

		return ret;
	}

	String RenderSystemCapabilities::vendorToString(GPUVendor v)
	{
		initVendorStrings();
		return msGPUVendorStrings[v];
	}

	void RenderSystemCapabilities::initVendorStrings()
	{
		if (msGPUVendorStrings.empty())
		{
			// Always lower case
			msGPUVendorStrings.resize(GPU_VENDOR_COUNT);
			msGPUVendorStrings[GPU_UNKNOWN] = "unknown";
			msGPUVendorStrings[GPU_NVIDIA] = "nvidia";
			msGPUVendorStrings[GPU_AMD] = "amd";
			msGPUVendorStrings[GPU_INTEL] = "intel";
		}
	}
}
