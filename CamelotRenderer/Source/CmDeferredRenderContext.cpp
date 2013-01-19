#include "CmDeferredRenderContext.h"
#include "CmCommandQueue.h"
#include "CmRenderSystem.h"
#include "CmBlendState.h"
#include "CmRasterizerState.h"
#include "CmDepthStencilState.h"
#include "CmGpuParams.h"

namespace CamelotEngine
{
	DeferredRenderContext::DeferredRenderContext(RenderSystem* rs, CM_THREAD_ID_TYPE threadId)
		:mCommandQueue(new CommandQueue(threadId))
		, mWaitForVerticalBlank(true)
		, mRenderSystem(rs)
	{
		assert(mRenderSystem != nullptr);
	}

	DeferredRenderContext::~DeferredRenderContext()
	{
		delete mCommandQueue;
	}

	void DeferredRenderContext::swapAllRenderTargetBuffers(bool waitForVSync)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::swapAllRenderTargetBuffers, mRenderSystem, waitForVSync));
	}

	void DeferredRenderContext::setViewport(const Viewport& vp)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setViewport, mRenderSystem, vp));
	}

	void DeferredRenderContext::setVertexBuffer(UINT32 index, const VertexBufferPtr& buffer)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setVertexBuffer, mRenderSystem, index, buffer));
	}

	void DeferredRenderContext::setIndexBuffer(const IndexBufferPtr& buffer)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setIndexBuffer, mRenderSystem, buffer));
	}

	void DeferredRenderContext::setVertexDeclaration(VertexDeclarationPtr vertexDeclaration)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setVertexDeclaration, mRenderSystem, vertexDeclaration));
	}

	void DeferredRenderContext::setDrawOperation(DrawOperationType op)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setDrawOperation, mRenderSystem, op));
	}

	void DeferredRenderContext::setSamplerState(GpuProgramType gptype, UINT16 texUnit, const SamplerStatePtr& samplerState)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setSamplerState, mRenderSystem, gptype, texUnit, samplerState));
	}

	void DeferredRenderContext::setBlendState(const BlendStatePtr& blendState)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setBlendState, mRenderSystem, blendState));
	}

	void DeferredRenderContext::setRasterizerState(const RasterizerStatePtr& rasterizerState)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setRasterizerState, mRenderSystem, rasterizerState));
	}

	void DeferredRenderContext::setDepthStencilState(const DepthStencilStatePtr& depthStencilState, UINT32 stencilRefValue)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setDepthStencilState, mRenderSystem, depthStencilState, stencilRefValue));
	}

	void DeferredRenderContext::setTexture(GpuProgramType gptype, UINT16 unit, bool enabled, const TexturePtr& texPtr)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setTexture, mRenderSystem, gptype, unit, enabled, texPtr));
	}

	void DeferredRenderContext::disableTextureUnit(GpuProgramType gptype, UINT16 texUnit)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::disableTextureUnit, mRenderSystem, gptype, texUnit));
	}

	void DeferredRenderContext::setScissorTest(UINT32 left, UINT32 top, UINT32 right, UINT32 bottom)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setScissorRect, mRenderSystem, left, top, right, bottom));
	}

	bool DeferredRenderContext::getWaitForVerticalBlank(void) const
	{
		return mWaitForVerticalBlank;
	}

	void DeferredRenderContext::setWaitForVerticalBlank(bool enabled)
	{
		mWaitForVerticalBlank = enabled;
		mCommandQueue->queue(boost::bind(&RenderSystem::setWaitForVerticalBlank, mRenderSystem, enabled));
	}

	void DeferredRenderContext::addClipPlane(const Plane &p)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::addClipPlane, mRenderSystem, p));
	}

	void DeferredRenderContext::addClipPlane(float A, float B, float C, float D)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::addClipPlane, mRenderSystem, A, B, C, D));
	}

	void DeferredRenderContext::setClipPlanes(const PlaneList& clipPlanes)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setClipPlanes, mRenderSystem, clipPlanes));
	}

	void DeferredRenderContext::resetClipPlanes()
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::resetClipPlanes, mRenderSystem));
	}

	void DeferredRenderContext::bindGpuProgram(GpuProgramHandle prg)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::bindGpuProgram, mRenderSystem, prg));
	}

	void DeferredRenderContext::unbindGpuProgram(GpuProgramType gptype)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::unbindGpuProgram, mRenderSystem, gptype));
	}

	void DeferredRenderContext::bindGpuParams(GpuProgramType gptype, GpuParamsPtr params)
	{
		GpuParamsPtr paramCopy = params->clone();
		mCommandQueue->queue(boost::bind(&RenderSystem::bindGpuParams, mRenderSystem, gptype, paramCopy));
	}

	void DeferredRenderContext::setRenderTarget(RenderTarget* target)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::setRenderTarget, mRenderSystem, target));
	}

	void DeferredRenderContext::clear(RenderTargetPtr target, unsigned int buffers, const Color& color, float depth, unsigned short stencil)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::clear, mRenderSystem, target, buffers, color, depth, stencil));
	}

	void DeferredRenderContext::beginFrame()
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::beginFrame, mRenderSystem));
	}

	void DeferredRenderContext::endFrame()
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::endFrame, mRenderSystem));
	}

	void DeferredRenderContext::render(const RenderOperation& op)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::render, mRenderSystem, op));
	}

	void DeferredRenderContext::draw(UINT32 vertexCount)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::draw, mRenderSystem, vertexCount));
	}

	void DeferredRenderContext::drawIndexed(UINT32 startIndex, UINT32 indexCount, UINT32 vertexCount)
	{
		mCommandQueue->queue(boost::bind(&RenderSystem::drawIndexed, mRenderSystem, startIndex, indexCount, vertexCount));
	}

	void DeferredRenderContext::submitToGpu()
	{
		vector<CommandQueue::Command>::type* commands = mCommandQueue->flush();

		RenderSystem* rs = RenderSystem::instancePtr();
		rs->queueCommand(boost::bind(&CommandQueue::playback, mCommandQueue, commands));
	}

	void DeferredRenderContext::cancelAll()
	{
		vector<CommandQueue::Command>::type* commands = mCommandQueue->flush();

		// TODO - This cancels the commands but doesn't revert the state variables
	}
}