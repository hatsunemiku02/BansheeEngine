#pragma once

#include "CmPrerequisites.h"

namespace CamelotEngine
{
	class CM_EXPORT GpuParams
	{
	public:
		GpuParams(GpuParamDesc& paramDesc);

		GpuParamBlockBufferPtr getParamBlock(UINT32 slot) const;
		GpuParamBlockBufferPtr getParamBlock(const String& name) const;

		void setParamBlock(UINT32 slot, GpuParamBlockBufferPtr paramBlock);
		void setParamBlock(const String& name, GpuParamBlockBufferPtr paramBlock);

		const GpuParamDesc& getParamDesc() const { return mParamDesc; }

		bool hasParam(const String& name) const;
		bool hasTexture(const String& name) const;
		bool hasSamplerState(const String& name) const;

		void setParam(const String& name, float value, UINT32 arrayIndex = 0);
		void setParam(const String& name, int value, UINT32 arrayIndex = 0);
		void setParam(const String& name, bool value, UINT32 arrayIndex = 0);
		void setParam(const String& name, const Vector4& vec, UINT32 arrayIndex = 0);
		void setParam(const String& name, const Vector3& vec, UINT32 arrayIndex = 0);
		void setParam(const String& name, const Vector2& vec, UINT32 arrayIndex = 0);
		void setParam(const String& name, const Matrix4& mat, UINT32 arrayIndex = 0);
		void setParam(const String& name, const Matrix3& mat, UINT32 arrayIndex = 0);
		void setParam(const String& name, const Color& color, UINT32 arrayIndex = 0);

		/**
		 * @brief	Sets a parameter.
		 *
		 * @param	name	  	Name of the parameter.
		 * @param	value	  	Parameter data.
		 * @param	size	  	Size of the provided data. It can be exact size or lower than the exact size of the wanted field.
		 * 						If it's lower unused bytes will be set to 0. 
		 * @param	arrayIndex	(optional) zero-based index of the array.
		 */
		void setParam(const String& name, const void* value, UINT32 sizeBytes, UINT32 arrayIndex = 0);

		void setTexture(const String& name, TextureHandle val);
		TextureHandle getTexture(UINT32 slot);

		void setSamplerState(const String& name, SamplerStatePtr val);
		SamplerStatePtr getSamplerState(UINT32 slot);

		void setTransposeMatrices(bool transpose) { mTransposeMatrices = transpose; }

		GpuParamsPtr clone() const;
		void updateIfDirty();

	private:
		GpuParamDesc& mParamDesc;
		bool mTransposeMatrices;

		GpuParamMemberDesc* getParamDesc(const String& name) const;

		vector<GpuParamBlockBufferPtr>::type mParamBlocks;
		vector<TextureHandle>::type mTextures;
		vector<SamplerStatePtr>::type mSamplerStates;
	};
}