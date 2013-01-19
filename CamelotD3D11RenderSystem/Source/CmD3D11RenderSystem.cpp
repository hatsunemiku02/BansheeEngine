#include "CmD3D11RenderSystem.h"
#include "CmD3D11DriverList.h"
#include "CmD3D11Driver.h"
#include "CmD3D11Device.h"
#include "CmD3D11TextureManager.h"
#include "CmD3D11Texture.h"
#include "CmD3D11HardwareBufferManager.h"
#include "CmD3D11GpuProgramManager.h"
#include "CmD3D11RenderWindowManager.h"
#include "CmD3D11HLSLProgramFactory.h"
#include "CmD3D11BlendState.h"
#include "CmD3D11RasterizerState.h"
#include "CmD3D11DepthStencilState.h"
#include "CmD3D11SamplerState.h"
#include "CmD3D11GpuProgram.h"
#include "CmD3D11Mappings.h"
#include "CmD3D11VertexBuffer.h"
#include "CmD3D11IndexBuffer.h"
#include "CmD3D11RenderStateManager.h"
#include "CmD3D11GpuParamBlock.h"
#include "CmD3D11InputLayoutManager.h"
#include "CmD3D11HLSLProgram.h"
#include "CmGpuParams.h"
#include "CmDebug.h"
#include "CmException.h"

#if CM_DEBUG_MODE
#define THROW_IF_NOT_RENDER_THREAD throwIfNotRenderThread();
#else
#define THROW_IF_NOT_RENDER_THREAD 
#endif

namespace CamelotEngine
{
	D3D11RenderSystem::D3D11RenderSystem()
		: mDXGIFactory(nullptr), mDevice(nullptr), mDriverList(nullptr)
		, mActiveD3DDriver(nullptr), mFeatureLevel(D3D_FEATURE_LEVEL_9_1)
		, mHLSLFactory(nullptr), mIAManager(nullptr)
		, mStencilRef(0), mActiveVertexShader(nullptr)
	{
		mClipPlanesDirty = false; // DX11 handles clip planes through shaders
	}

	D3D11RenderSystem::~D3D11RenderSystem()
	{
		destroy_internal();
	}

	const String& D3D11RenderSystem::getName() const
	{
		static String strName("D3D11RenderSystem");
		return strName;
	}

	const String& D3D11RenderSystem::getShadingLanguageName() const
	{
		static String strName("hlsl");
		return strName;
	}

	void D3D11RenderSystem::initialize_internal()
	{
		THROW_IF_NOT_RENDER_THREAD;

		HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&mDXGIFactory);
		if(FAILED(hr))
			CM_EXCEPT(RenderingAPIException, "Failed to create Direct3D11 DXGIFactory");

		mDriverList = new D3D11DriverList(mDXGIFactory);
		mActiveD3DDriver = mDriverList->item(0); // TODO: Always get first driver, for now

		IDXGIAdapter* selectedAdapter = mActiveD3DDriver->getDeviceAdapter();

		D3D_FEATURE_LEVEL requestedLevels[] = {
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			D3D_FEATURE_LEVEL_9_3,
			D3D_FEATURE_LEVEL_9_2,
			D3D_FEATURE_LEVEL_9_1
		};

		UINT32 numRequestedLevel = sizeof(requestedLevels) / sizeof(requestedLevels[0]);

		UINT32 deviceFlags = 0;

#if CM_DEBUG_MODE
		deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		ID3D11Device* device;
		hr = D3D11CreateDevice(selectedAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, deviceFlags, 
			requestedLevels, numRequestedLevel, D3D11_SDK_VERSION, &device, &mFeatureLevel, nullptr);

		if(FAILED(hr))         
			CM_EXCEPT(RenderingAPIException, "Failed to create Direct3D11 object. D3D11CreateDeviceN returned this error code: " + toString(hr));

		mDevice = new D3D11Device(device);

		LARGE_INTEGER driverVersion;
		if(SUCCEEDED(selectedAdapter->CheckInterfaceSupport(IID_ID3D10Device /* intentionally D3D10, not D3D11 */, &driverVersion)))
		{
			mDriverVersion.major = HIWORD(driverVersion.HighPart);
			mDriverVersion.minor = LOWORD(driverVersion.HighPart);
			mDriverVersion.release = HIWORD(driverVersion.LowPart);
			mDriverVersion.build = LOWORD(driverVersion.LowPart);
		}

		// Create the texture manager for use by others		
		TextureManager::startUp(new D3D11TextureManager());

		// Also create hardware buffer manager		
		HardwareBufferManager::startUp(new D3D11HardwareBufferManager(*mDevice));

		// Create the GPU program manager		
		GpuProgramManager::startUp(new D3D11GpuProgramManager(*mDevice));

		// Create render window manager
		RenderWindowManager::startUp(new D3D11RenderWindowManager(this));

		// Create & register HLSL factory		
		mHLSLFactory = new D3D11HLSLProgramFactory();

		// Create render state manager
		RenderStateManager::startUp(new D3D11RenderStateManager());

		mCurrentCapabilities = createRenderSystemCapabilities();

		mCurrentCapabilities->addShaderProfile("hlsl");
		HighLevelGpuProgramManager::instance().addFactory(mHLSLFactory);

		mIAManager = new D3D11InputLayoutManager();

		RenderSystem::initialize_internal();
	}

    void D3D11RenderSystem::destroy_internal()
	{
		THROW_IF_NOT_RENDER_THREAD;

		SAFE_DELETE(mIAManager);
		SAFE_DELETE(mHLSLFactory);

		RenderStateManager::shutDown();
		RenderWindowManager::shutDown();
		GpuProgramManager::shutDown();
		HardwareBufferManager::shutDown();
		TextureManager::shutDown();

		SAFE_RELEASE(mDXGIFactory);
		SAFE_DELETE(mDevice);
		SAFE_DELETE(mDriverList);
		mActiveD3DDriver = nullptr;

		RenderSystem::destroy_internal();
	}

	void D3D11RenderSystem::setSamplerState(GpuProgramType gptype, UINT16 texUnit, const SamplerStatePtr& samplerState)
	{
		THROW_IF_NOT_RENDER_THREAD;

		// TODO - I'm setting up views one by one, it might be more efficient to hold them in an array
		//  and then set them all up at once before rendering? Needs testing

		ID3D11SamplerState* samplerArray[1];
		D3D11SamplerState* d3d11SamplerState = static_cast<D3D11SamplerState*>(const_cast<SamplerState*>(samplerState.get()));
		samplerArray[0] = d3d11SamplerState->getInternal();

		switch(gptype)
		{
		case GPT_VERTEX_PROGRAM:
			mDevice->getImmediateContext()->VSSetSamplers(texUnit, 1, samplerArray);
			break;
		case GPT_FRAGMENT_PROGRAM:
			mDevice->getImmediateContext()->PSSetSamplers(texUnit, 1, samplerArray);
			break;
		case GPT_GEOMETRY_PROGRAM:
			mDevice->getImmediateContext()->GSSetSamplers(texUnit, 1, samplerArray);
			break;
		case GPT_DOMAIN_PROGRAM:
			mDevice->getImmediateContext()->DSSetSamplers(texUnit, 1, samplerArray);
			break;
		case GPT_HULL_PROGRAM:
			mDevice->getImmediateContext()->HSSetSamplers(texUnit, 1, samplerArray);
			break;
		case GPT_COMPUTE_PROGRAM:
			mDevice->getImmediateContext()->CSSetSamplers(texUnit, 1, samplerArray);
			break;
		default:
			CM_EXCEPT(InvalidParametersException, "Unsupported gpu program type: " + toString(gptype));
		}
	}

	void D3D11RenderSystem::setBlendState(const BlendStatePtr& blendState)
	{
		THROW_IF_NOT_RENDER_THREAD;

		D3D11BlendState* d3d11BlendState = static_cast<D3D11BlendState*>(const_cast<BlendState*>(blendState.get()));
		mDevice->getImmediateContext()->OMSetBlendState(d3d11BlendState->getInternal(), nullptr, 0xFFFFFFFF);
	}

	void D3D11RenderSystem::setRasterizerState(const RasterizerStatePtr& rasterizerState)
	{
		THROW_IF_NOT_RENDER_THREAD;

		D3D11RasterizerState* d3d11RasterizerState = static_cast<D3D11RasterizerState*>(const_cast<RasterizerState*>(rasterizerState.get()));
		mDevice->getImmediateContext()->RSSetState(d3d11RasterizerState->getInternal());
	}

	void D3D11RenderSystem::setDepthStencilState(const DepthStencilStatePtr& depthStencilState, UINT32 stencilRefValue)
	{
		THROW_IF_NOT_RENDER_THREAD;

		D3D11DepthStencilState* d3d11RasterizerState = static_cast<D3D11DepthStencilState*>(const_cast<DepthStencilState*>(depthStencilState.get()));
		mDevice->getImmediateContext()->OMSetDepthStencilState(d3d11RasterizerState->getInternal(), stencilRefValue);
	}

	void D3D11RenderSystem::setTexture(GpuProgramType gptype, UINT16 unit, bool enabled, const TexturePtr &texPtr)
	{
		THROW_IF_NOT_RENDER_THREAD;

		// TODO - Set up UAVs?
		// TODO - I'm setting up views one by one, it might be more efficient to hold them in an array
		//  and then set them all up at once before rendering? Needs testing

		ID3D11ShaderResourceView* viewArray[1];
		if(texPtr != nullptr && enabled)
		{
			D3D11Texture* d3d11Texture = static_cast<D3D11Texture*>(texPtr.get());
			viewArray[0] = d3d11Texture->getSRV();
		}
		else
			viewArray[0] = nullptr;

		switch(gptype)
		{
		case GPT_VERTEX_PROGRAM:
			mDevice->getImmediateContext()->VSSetShaderResources(unit, 1, viewArray);
			break;
		case GPT_FRAGMENT_PROGRAM:
			mDevice->getImmediateContext()->PSSetShaderResources(unit, 1, viewArray);
			break;
		case GPT_GEOMETRY_PROGRAM:
			mDevice->getImmediateContext()->GSSetShaderResources(unit, 1, viewArray);
			break;
		case GPT_DOMAIN_PROGRAM:
			mDevice->getImmediateContext()->DSSetShaderResources(unit, 1, viewArray);
			break;
		case GPT_HULL_PROGRAM:
			mDevice->getImmediateContext()->HSSetShaderResources(unit, 1, viewArray);
			break;
		case GPT_COMPUTE_PROGRAM:
			mDevice->getImmediateContext()->CSSetShaderResources(unit, 1, viewArray);
			break;
		default:
			CM_EXCEPT(InvalidParametersException, "Unsupported gpu program type: " + toString(gptype));
		}
	}

	void D3D11RenderSystem::disableTextureUnit(GpuProgramType gptype, UINT16 texUnit)
	{
		THROW_IF_NOT_RENDER_THREAD;

		setTexture(gptype, texUnit, false, nullptr);
	}

	void D3D11RenderSystem::beginFrame()
	{
		// Not used
	}

	void D3D11RenderSystem::endFrame()
	{
		// Not used
	}

	void D3D11RenderSystem::setViewport(const Viewport& vp)
	{
		THROW_IF_NOT_RENDER_THREAD;

		mActiveViewport = vp;

		// Set render target
		RenderTargetPtr target = vp.getTarget();
		setRenderTarget(target.get());

		// set viewport dimensions
		mViewport.TopLeftX = (FLOAT)vp.getActualLeft();
		mViewport.TopLeftY = (FLOAT)vp.getActualTop();
		mViewport.Width = (FLOAT)vp.getActualWidth();
		mViewport.Height = (FLOAT)vp.getActualHeight();

		if (vp.getTarget()->requiresTextureFlipping())
		{
			// Convert "top-left" to "bottom-left"
			mViewport.TopLeftY = vp.getTarget()->getHeight() - mViewport.Height - mViewport.TopLeftY;
		}

		// Z-values from 0.0 to 1.0 (TODO: standardise with OpenGL)
		mViewport.MinDepth = 0.0f;
		mViewport.MaxDepth = 1.0f;

		mDevice->getImmediateContext()->RSSetViewports(1, &mViewport);
	}

	void D3D11RenderSystem::setVertexBuffer(UINT32 index, const VertexBufferPtr& buffer)
	{
		THROW_IF_NOT_RENDER_THREAD;

		UINT32 maxBoundVertexBuffers = mCurrentCapabilities->getMaxBoundVertexBuffers();
		if(index < 0 || index >= maxBoundVertexBuffers)
			CM_EXCEPT(InvalidParametersException, "Invalid vertex index: " + toString(index) + ". Valid range is 0 .. " + toString(maxBoundVertexBuffers - 1));

		ID3D11Buffer* buffers[1];
		D3D11VertexBuffer* vertexBuffer = static_cast<D3D11VertexBuffer*>(buffer.get());
		buffers[0] = vertexBuffer->getD3DVertexBuffer();

		UINT32 strides[1] = { buffer->getVertexSize() };
		UINT32 offsets[1] = { 0 };

		mDevice->getImmediateContext()->IASetVertexBuffers(index, 1, buffers, strides, offsets);
	}

	void D3D11RenderSystem::setIndexBuffer(const IndexBufferPtr& buffer)
	{
		THROW_IF_NOT_RENDER_THREAD;

		D3D11IndexBuffer* indexBuffer = static_cast<D3D11IndexBuffer*>(buffer.get());

		DXGI_FORMAT indexFormat = DXGI_FORMAT_R16_UINT;
		if(indexBuffer->getType() == IndexBuffer::IT_16BIT)
			indexFormat = DXGI_FORMAT_R16_UINT;
		else if(indexBuffer->getType() == IndexBuffer::IT_32BIT)
			indexFormat = DXGI_FORMAT_R32_UINT;
		else
			CM_EXCEPT(InternalErrorException, "Unsupported index format: " + toString(indexBuffer->getType()));

		mDevice->getImmediateContext()->IASetIndexBuffer(indexBuffer->getD3DIndexBuffer(), indexFormat, 0);
	}

	void D3D11RenderSystem::setVertexDeclaration(VertexDeclarationPtr vertexDeclaration)
	{
		THROW_IF_NOT_RENDER_THREAD;

		mActiveVertexDeclaration = vertexDeclaration;
	}

	void D3D11RenderSystem::setDrawOperation(DrawOperationType op)
	{
		THROW_IF_NOT_RENDER_THREAD;

		mDevice->getImmediateContext()->IASetPrimitiveTopology(D3D11Mappings::getPrimitiveType(op));
	}

	void D3D11RenderSystem::bindGpuProgram(GpuProgramHandle prg)
	{
		THROW_IF_NOT_RENDER_THREAD;

		switch(prg->getType())
		{
		case GPT_VERTEX_PROGRAM:
			{
				D3D11GpuVertexProgram* d3d11GpuProgram = static_cast<D3D11GpuVertexProgram*>(prg->getBindingDelegate());
				mDevice->getImmediateContext()->VSSetShader(d3d11GpuProgram->getVertexShader(), nullptr, 0);
				mActiveVertexShader = static_cast<D3D11HLSLProgram*>(prg.get());
				break;
			}
		case GPT_FRAGMENT_PROGRAM:
			{
				D3D11GpuFragmentProgram* d3d11GpuProgram = static_cast<D3D11GpuFragmentProgram*>(prg->getBindingDelegate());
				mDevice->getImmediateContext()->PSSetShader(d3d11GpuProgram->getPixelShader(), nullptr, 0);
				break;
			}
		case GPT_GEOMETRY_PROGRAM:
			{
				D3D11GpuGeometryProgram* d3d11GpuProgram = static_cast<D3D11GpuGeometryProgram*>(prg->getBindingDelegate());
				mDevice->getImmediateContext()->GSSetShader(d3d11GpuProgram->getGeometryShader(), nullptr, 0);
				break;
			}
		case GPT_DOMAIN_PROGRAM:
			{
				D3D11GpuDomainProgram* d3d11GpuProgram = static_cast<D3D11GpuDomainProgram*>(prg->getBindingDelegate());
				mDevice->getImmediateContext()->DSSetShader(d3d11GpuProgram->getDomainShader(), nullptr, 0);
				break;
			}
		case GPT_HULL_PROGRAM:
			{
				D3D11GpuHullProgram* d3d11GpuProgram = static_cast<D3D11GpuHullProgram*>(prg->getBindingDelegate());
				mDevice->getImmediateContext()->HSSetShader(d3d11GpuProgram->getHullShader(), nullptr, 0);
				break;
			}
		case GPT_COMPUTE_PROGRAM:
			{
				D3D11GpuComputeProgram* d3d11GpuProgram = static_cast<D3D11GpuComputeProgram*>(prg->getBindingDelegate());
				mDevice->getImmediateContext()->CSSetShader(d3d11GpuProgram->getComputeShader(), nullptr, 0);
				break;
			}
		default:
			CM_EXCEPT(InvalidParametersException, "Unsupported gpu program type: " + toString(prg->getType()));
		}
	}

	void D3D11RenderSystem::unbindGpuProgram(GpuProgramType gptype)
	{
		THROW_IF_NOT_RENDER_THREAD;

		switch(gptype)
		{
		case GPT_VERTEX_PROGRAM:
			mDevice->getImmediateContext()->VSSetShader(nullptr, nullptr, 0);
			mActiveVertexShader = nullptr;
			break;
		case GPT_FRAGMENT_PROGRAM:
			mDevice->getImmediateContext()->PSSetShader(nullptr, nullptr, 0);
			break;
		case GPT_GEOMETRY_PROGRAM:
			mDevice->getImmediateContext()->GSSetShader(nullptr, nullptr, 0);
			break;
		case GPT_DOMAIN_PROGRAM:
			mDevice->getImmediateContext()->DSSetShader(nullptr, nullptr, 0);
			break;
		case GPT_HULL_PROGRAM:
			mDevice->getImmediateContext()->HSSetShader(nullptr, nullptr, 0);
			break;
		case GPT_COMPUTE_PROGRAM:
			mDevice->getImmediateContext()->CSSetShader(nullptr, nullptr, 0);
			break;
		default:
			CM_EXCEPT(InvalidParametersException, "Unsupported gpu program type: " + toString(gptype));
		}
	}

	void D3D11RenderSystem::bindGpuParams(GpuProgramType gptype, GpuParamsPtr params)
	{
		THROW_IF_NOT_RENDER_THREAD;

		params->updateIfDirty();

		const GpuParamDesc& paramDesc = params->getParamDesc();

		for(auto iter = paramDesc.samplers.begin(); iter != paramDesc.samplers.end(); ++iter)
		{
			SamplerStatePtr samplerState = params->getSamplerState(iter->second.slot);

			if(samplerState == nullptr)
				setSamplerState(gptype, iter->second.slot, SamplerState::getDefault());
			else
				setSamplerState(gptype, iter->second.slot, samplerState);
		}

		for(auto iter = paramDesc.textures.begin(); iter != paramDesc.textures.end(); ++iter)
		{
			TextureHandle texture = params->getTexture(iter->second.slot);

			if(!texture.isLoaded())
				setTexture(gptype, iter->second.slot, false, nullptr);
			else
				setTexture(gptype, iter->second.slot, true, texture.getInternalPtr());
		}

		// TODO - I assign constant buffers one by one but it might be more efficient to do them all at once?

		ID3D11Buffer* bufferArray[1];

		for(auto iter = paramDesc.paramBlocks.begin(); iter != paramDesc.paramBlocks.end(); ++iter)
		{
			GpuParamBlockBufferPtr currentBlock = params->getParamBlock(iter->second.slot);

			if(currentBlock != nullptr)
			{
				D3D11GpuParamBlock* d3d11paramBlock = static_cast<D3D11GpuParamBlock*>(currentBlock.get());
				bufferArray[0] = d3d11paramBlock->getD3D11Buffer();
			}
			else
				bufferArray[0] = nullptr;

			switch(gptype)
			{
			case GPT_VERTEX_PROGRAM:
				mDevice->getImmediateContext()->VSSetConstantBuffers(iter->second.slot, 1, bufferArray);
				break;
			case GPT_FRAGMENT_PROGRAM:
				mDevice->getImmediateContext()->PSSetConstantBuffers(iter->second.slot, 1, bufferArray);
				break;
			case GPT_GEOMETRY_PROGRAM:
				mDevice->getImmediateContext()->GSSetConstantBuffers(iter->second.slot, 1, bufferArray);
				break;
			case GPT_HULL_PROGRAM:
				mDevice->getImmediateContext()->HSSetConstantBuffers(iter->second.slot, 1, bufferArray);
				break;
			case GPT_DOMAIN_PROGRAM:
				mDevice->getImmediateContext()->DSSetConstantBuffers(iter->second.slot, 1, bufferArray);
				break;
			case GPT_COMPUTE_PROGRAM:
				mDevice->getImmediateContext()->CSSetConstantBuffers(iter->second.slot, 1, bufferArray);
				break;
			};
		}
	}

	void D3D11RenderSystem::draw(UINT32 vertexCount)
	{
		THROW_IF_NOT_RENDER_THREAD;

		applyInputLayout();

		mDevice->getImmediateContext()->Draw(vertexCount, 0);
	}

	void D3D11RenderSystem::drawIndexed(UINT32 startIndex, UINT32 indexCount, UINT32 vertexCount)
	{
		THROW_IF_NOT_RENDER_THREAD;

		applyInputLayout();

		mDevice->getImmediateContext()->DrawIndexed(indexCount, startIndex, 0);
	}

	void D3D11RenderSystem::setScissorRect(UINT32 left, UINT32 top, UINT32 right, UINT32 bottom)
	{
		THROW_IF_NOT_RENDER_THREAD;

		mScissorRect.left = static_cast<LONG>(left);
		mScissorRect.top = static_cast<LONG>(top);
		mScissorRect.bottom = static_cast<LONG>(bottom);
		mScissorRect.right = static_cast<LONG>(right);

		mDevice->getImmediateContext()->RSSetScissorRects(1, &mScissorRect);
	}

	void D3D11RenderSystem::clear(RenderTargetPtr target, unsigned int buffers, const Color& color, float depth, unsigned short stencil)
	{
		THROW_IF_NOT_RENDER_THREAD;

		// Clear render surfaces
		if (buffers & FBT_COLOUR)
		{
			UINT32 maxRenderTargets = mCurrentCapabilities->getNumMultiRenderTargets();

			ID3D11RenderTargetView** views = new ID3D11RenderTargetView*[maxRenderTargets];
			memset(views, 0, sizeof(ID3D11RenderTargetView*) * maxRenderTargets);

			target->getCustomAttribute("RTV", views);
			if (!views[0])
			{
				delete[] views;
				return;
			}

			float clearColor[4];
			clearColor[0] = color.r;
			clearColor[1] = color.g;
			clearColor[2] = color.b;
			clearColor[3] = color.a;

			for(UINT32 i = 0; i < maxRenderTargets; i++)
			{
				if(views[i] != nullptr)
					mDevice->getImmediateContext()->ClearRenderTargetView(views[i], clearColor);
			}

			delete[] views;
		}

		// Clear depth stencil
		if((buffers & FBT_DEPTH) != 0 || (buffers & FBT_STENCIL) != 0)
		{
			ID3D11DepthStencilView* depthStencilView = nullptr;
			target->getCustomAttribute("DSV", &depthStencilView);

			D3D11_CLEAR_FLAG clearFlag;

			if((buffers & FBT_DEPTH) != 0 && (buffers & FBT_STENCIL) != 0)
				clearFlag = (D3D11_CLEAR_FLAG)(D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL);
			else if((buffers & FBT_STENCIL) != 0)
				clearFlag = D3D11_CLEAR_STENCIL;
			else
				clearFlag = D3D11_CLEAR_DEPTH;

			if(depthStencilView != nullptr)
				mDevice->getImmediateContext()->ClearDepthStencilView(depthStencilView, clearFlag, depth, (UINT8)stencil);
		}
	}

	void D3D11RenderSystem::setRenderTarget(RenderTarget* target)
	{
		THROW_IF_NOT_RENDER_THREAD;

		mActiveRenderTarget = target;

		// Retrieve render surfaces
		UINT32 maxRenderTargets = mCurrentCapabilities->getNumMultiRenderTargets();
		ID3D11RenderTargetView** views = new ID3D11RenderTargetView*[maxRenderTargets];
		memset(views, 0, sizeof(ID3D11RenderTargetView*) * maxRenderTargets);
		target->getCustomAttribute("RTV", views);
		if (!views[0])
		{
			delete[] views;
			return;
		}

		// Retrieve depth stencil
		ID3D11DepthStencilView* depthStencilView = nullptr;
		target->getCustomAttribute("DSV", &depthStencilView);

		// Bind render targets
		mDevice->getImmediateContext()->OMSetRenderTargets(maxRenderTargets, views, depthStencilView);
		if (mDevice->hasError())
			CM_EXCEPT(RenderingAPIException, "Failed to setRenderTarget : " + mDevice->getErrorDescription());

		delete[] views;
	}

	void D3D11RenderSystem::setClipPlanesImpl(const PlaneList& clipPlanes)
	{
		LOGWRN("This call will be ignored. DX11 uses shaders for setting clip planes.");
	}

	RenderSystemCapabilities* D3D11RenderSystem::createRenderSystemCapabilities() const
	{
		THROW_IF_NOT_RENDER_THREAD;

		RenderSystemCapabilities* rsc = new RenderSystemCapabilities();

		rsc->setDriverVersion(mDriverVersion);
		rsc->setDeviceName(mActiveD3DDriver->getDriverDescription());
		rsc->setRenderSystemName(getName());

		rsc->setCapability(RSC_HWSTENCIL);
		rsc->setStencilBufferBitDepth(8);

		rsc->setCapability(RSC_ANISOTROPY);
		rsc->setCapability(RSC_AUTOMIPMAP);
		rsc->setCapability(RSC_BLENDING);
		rsc->setCapability(RSC_DOT3);

		// Cube map
		rsc->setCapability(RSC_CUBEMAPPING);

		// We always support compression, D3DX will decompress if device does not support
		rsc->setCapability(RSC_TEXTURE_COMPRESSION);
		rsc->setCapability(RSC_TEXTURE_COMPRESSION_DXT);
		rsc->setCapability(RSC_VBO);
		rsc->setCapability(RSC_SCISSOR_TEST);
		rsc->setCapability(RSC_TWO_SIDED_STENCIL);
		rsc->setCapability(RSC_STENCIL_WRAP);
		rsc->setCapability(RSC_HWOCCLUSION);
		rsc->setCapability(RSC_HWOCCLUSION_ASYNCHRONOUS);

		if(mFeatureLevel >= D3D_FEATURE_LEVEL_10_1)
			rsc->setMaxBoundVertexBuffers(32);
		else
			rsc->setMaxBoundVertexBuffers(16);

		if(mFeatureLevel >= D3D_FEATURE_LEVEL_10_0)
		{
			rsc->addShaderProfile("ps_4_0");
			rsc->addShaderProfile("vs_4_0");
			rsc->addShaderProfile("gs_4_0");

			rsc->addGpuProgramProfile(GPP_PS_4_0, "ps_4_0");
			rsc->addGpuProgramProfile(GPP_VS_4_0, "vs_4_0");
			rsc->addGpuProgramProfile(GPP_GS_4_0, "gs_4_0");

			rsc->setNumTextureUnits(GPT_FRAGMENT_PROGRAM, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
			rsc->setNumTextureUnits(GPT_VERTEX_PROGRAM, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
			rsc->setNumTextureUnits(GPT_GEOMETRY_PROGRAM, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

			rsc->setNumCombinedTextureUnits(rsc->getNumTextureUnits(GPT_FRAGMENT_PROGRAM)
				+ rsc->getNumTextureUnits(GPT_VERTEX_PROGRAM) + rsc->getNumTextureUnits(GPT_VERTEX_PROGRAM));

			rsc->setNumUniformBlockBuffers(GPT_FRAGMENT_PROGRAM, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
			rsc->setNumUniformBlockBuffers(GPT_VERTEX_PROGRAM, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
			rsc->setNumUniformBlockBuffers(GPT_GEOMETRY_PROGRAM, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);

			rsc->setNumCombinedUniformBlockBuffers(rsc->getNumUniformBlockBuffers(GPT_FRAGMENT_PROGRAM)
				+ rsc->getNumUniformBlockBuffers(GPT_VERTEX_PROGRAM) + rsc->getNumUniformBlockBuffers(GPT_VERTEX_PROGRAM));
		}

		if(mFeatureLevel >= D3D_FEATURE_LEVEL_10_1)
		{
			rsc->addShaderProfile("ps_4_1");
			rsc->addShaderProfile("vs_4_1");
			rsc->addShaderProfile("gs_4_1");

			rsc->addGpuProgramProfile(GPP_PS_4_1, "ps_4_1");
			rsc->addGpuProgramProfile(GPP_VS_4_1, "vs_4_1");
			rsc->addGpuProgramProfile(GPP_GS_4_1, "gs_4_1");
		}

		if(mFeatureLevel >= D3D_FEATURE_LEVEL_11_0)
		{
			rsc->addShaderProfile("ps_5_0");
			rsc->addShaderProfile("vs_5_0");
			rsc->addShaderProfile("gs_5_0");
			rsc->addShaderProfile("cs_5_0");
			rsc->addShaderProfile("hs_5_0");
			rsc->addShaderProfile("ds_5_0");

			rsc->addGpuProgramProfile(GPP_PS_5_0, "ps_5_0");
			rsc->addGpuProgramProfile(GPP_VS_5_0, "vs_5_0");
			rsc->addGpuProgramProfile(GPP_GS_5_0, "gs_5_0");
			rsc->addGpuProgramProfile(GPP_CS_5_0, "cs_5_0");
			rsc->addGpuProgramProfile(GPP_HS_5_0, "hs_5_0");
			rsc->addGpuProgramProfile(GPP_DS_5_0, "ds_5_0");

			rsc->setNumTextureUnits(GPT_HULL_PROGRAM, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
			rsc->setNumTextureUnits(GPT_DOMAIN_PROGRAM, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
			rsc->setNumTextureUnits(GPT_COMPUTE_PROGRAM, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);

			rsc->setNumCombinedTextureUnits(rsc->getNumTextureUnits(GPT_FRAGMENT_PROGRAM)
				+ rsc->getNumTextureUnits(GPT_VERTEX_PROGRAM) + rsc->getNumTextureUnits(GPT_VERTEX_PROGRAM)
				+ rsc->getNumTextureUnits(GPT_HULL_PROGRAM) + rsc->getNumTextureUnits(GPT_DOMAIN_PROGRAM)
				+ rsc->getNumTextureUnits(GPT_COMPUTE_PROGRAM));

			rsc->setNumUniformBlockBuffers(GPT_HULL_PROGRAM, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
			rsc->setNumUniformBlockBuffers(GPT_DOMAIN_PROGRAM, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
			rsc->setNumUniformBlockBuffers(GPT_COMPUTE_PROGRAM, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);

			rsc->setNumCombinedUniformBlockBuffers(rsc->getNumUniformBlockBuffers(GPT_FRAGMENT_PROGRAM)
				+ rsc->getNumUniformBlockBuffers(GPT_VERTEX_PROGRAM) + rsc->getNumUniformBlockBuffers(GPT_VERTEX_PROGRAM)
				+ rsc->getNumUniformBlockBuffers(GPT_HULL_PROGRAM) + rsc->getNumUniformBlockBuffers(GPT_DOMAIN_PROGRAM)
				+ rsc->getNumUniformBlockBuffers(GPT_COMPUTE_PROGRAM));

			rsc->setCapability(RSC_SHADER_SUBROUTINE);
		}

		rsc->setCapability(RSC_USER_CLIP_PLANES);
		rsc->setCapability(RSC_VERTEX_FORMAT_UBYTE4);

		// Adapter details
		const DXGI_ADAPTER_DESC& adapterID = mActiveD3DDriver->getAdapterIdentifier();

		// determine vendor
		// Full list of vendors here: http://www.pcidatabase.com/vendors.php?sort=id
		switch(adapterID.VendorId)
		{
		case 0x10DE:
			rsc->setVendor(GPU_NVIDIA);
			break;
		case 0x1002:
			rsc->setVendor(GPU_ATI);
			break;
		case 0x163C:
		case 0x8086:
			rsc->setVendor(GPU_INTEL);
			break;
		case 0x5333:
			rsc->setVendor(GPU_S3);
			break;
		case 0x3D3D:
			rsc->setVendor(GPU_3DLABS);
			break;
		case 0x102B:
			rsc->setVendor(GPU_MATROX);
			break;
		default:
			rsc->setVendor(GPU_UNKNOWN);
			break;
		};

		rsc->setCapability(RSC_INFINITE_FAR_PLANE);

		rsc->setCapability(RSC_TEXTURE_3D);
		rsc->setCapability(RSC_NON_POWER_OF_2_TEXTURES);
		rsc->setCapability(RSC_HWRENDER_TO_TEXTURE);
		rsc->setCapability(RSC_TEXTURE_FLOAT);

		rsc->setNumMultiRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
		rsc->setCapability(RSC_MRT_DIFFERENT_BIT_DEPTHS);

		rsc->setCapability(RSC_POINT_SPRITES);
		rsc->setCapability(RSC_POINT_EXTENDED_PARAMETERS);
		rsc->setMaxPointSize(256);

		rsc->setCapability(RSC_VERTEX_TEXTURE_FETCH);

		rsc->setCapability(RSC_MIPMAP_LOD_BIAS);

		rsc->setCapability(RSC_PERSTAGECONSTANT);

		return rsc;
	}

	void D3D11RenderSystem::initialiseFromRenderSystemCapabilities(RenderSystemCapabilities* caps)
	{
		// Do nothing
	}

	String D3D11RenderSystem::getErrorDescription(long errorNumber) const
	{
		return mDevice->getErrorDescription();
	}

	void D3D11RenderSystem::determineFSAASettings(UINT32 fsaa, const String& fsaaHint, DXGI_FORMAT format, DXGI_SAMPLE_DESC* outFSAASettings)
	{
		bool ok = false;
		bool qualityHint = fsaaHint.find("Quality") != String::npos;
		size_t origFSAA = fsaa;
		bool tryCSAA = false;
		// NVIDIA, prefer CSAA if available for 8+
		// it would be tempting to use getCapabilities()->getVendor() == GPU_NVIDIA but
		// if this is the first window, caps will not be initialised yet
		
		if (mActiveD3DDriver->getAdapterIdentifier().VendorId == 0x10DE && 
			fsaa >= 8)
		{
			tryCSAA	 = true;
		}

		while (!ok)
		{
			// Deal with special cases
			if (tryCSAA)
			{
				// see http://developer.nvidia.com/object/coverage-sampled-aa.html
				switch(fsaa)
				{
				case 8:
					if (qualityHint)
					{
						outFSAASettings->Count = 8;
						outFSAASettings->Quality = 8;
					}
					else
					{
						outFSAASettings->Count = 4;
						outFSAASettings->Quality = 8;
					}
					break;
				case 16:
					if (qualityHint)
					{
						outFSAASettings->Count = 8;
						outFSAASettings->Quality = 16;
					}
					else
					{
						outFSAASettings->Count = 4;
						outFSAASettings->Quality = 16;
					}
					break;
				}
			}
			else // !CSAA
			{
				outFSAASettings->Count = fsaa == 0 ? 1 : fsaa;
				outFSAASettings->Quality = 0;
			}


			HRESULT hr;
			UINT outQuality;
			hr = mDevice->getD3D11Device()->CheckMultisampleQualityLevels(format, outFSAASettings->Count, &outQuality);

			if (SUCCEEDED(hr) && (!tryCSAA || outQuality > outFSAASettings->Quality))
			{
				ok = true;
			}
			else
			{
				// downgrade
				if (tryCSAA && fsaa == 8)
				{
					// for CSAA, we'll try downgrading with quality mode at all samples.
					// then try without quality, then drop CSAA
					if (qualityHint)
					{
						// drop quality first
						qualityHint = false;
					}
					else
					{
						// drop CSAA entirely 
						tryCSAA = false;
					}
					// return to original requested samples
					fsaa = static_cast<UINT32>(origFSAA);
				}
				else
				{
					// drop samples
					--fsaa;

					if (fsaa == 1)
					{
						// ran out of options, no FSAA
						fsaa = 0;
						ok = true;
					}
				}
			}

		} // while !ok
	}

	bool D3D11RenderSystem::checkTextureFilteringSupported(TextureType ttype, PixelFormat format, int usage)
	{
		return true;
	}

	VertexElementType D3D11RenderSystem::getColorVertexElementType() const
	{
		return VET_COLOR_ABGR;
	}

	void D3D11RenderSystem::convertProjectionMatrix(const Matrix4& matrix, Matrix4& dest, bool forGpuProgram /*= false */)
	{
		dest = matrix;

		// Convert depth range from [-1,+1] to [0,1]
		dest[2][0] = (dest[2][0] + dest[3][0]) / 2;
		dest[2][1] = (dest[2][1] + dest[3][1]) / 2;
		dest[2][2] = (dest[2][2] + dest[3][2]) / 2;
		dest[2][3] = (dest[2][3] + dest[3][3]) / 2;

		if (!forGpuProgram)
		{
			// Convert right-handed to left-handed
			dest[0][2] = -dest[0][2];
			dest[1][2] = -dest[1][2];
			dest[2][2] = -dest[2][2];
			dest[3][2] = -dest[3][2];
		}
	}

	float D3D11RenderSystem::getHorizontalTexelOffset()
	{
		return 0.0f;
	}

	float D3D11RenderSystem::getVerticalTexelOffset()
	{
		return 0.0f;
	}

	float D3D11RenderSystem::getMinimumDepthInputValue()
	{
		return 0.0f;
	}

	float D3D11RenderSystem::getMaximumDepthInputValue()
	{
		return -1.0f;
	}

	/************************************************************************/
	/* 								PRIVATE		                     		*/
	/************************************************************************/

	void D3D11RenderSystem::applyInputLayout()
	{
		if(mActiveVertexDeclaration == nullptr)
		{
			LOGWRN("Cannot apply input layout without a vertex declaration. Set vertex declaration before calling this method.");
			return;
		}

		ID3D11InputLayout* ia = mIAManager->retrieveInputLayout(mActiveVertexShader->getInputDeclaration(), mActiveVertexDeclaration, *mActiveVertexShader);

		mDevice->getImmediateContext()->IASetInputLayout(ia);
	}
}

#undef THROW_IF_NOT_RENDER_THREAD