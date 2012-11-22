/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2011 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#pragma once

#include "CmPrerequisites.h"
#include "CmCommon.h"
#include "CmMatrix4.h"
#include "CmString.h"
#include "CmPixelUtil.h"
#include "CmTexture.h"

namespace CamelotEngine {
	/** \addtogroup Core
	*  @{
	*/
	/** \addtogroup Materials
	*  @{
	*/
	/** Class representing the state of a single sampler unit during a Pass of a
        Technique, of a Material.
    @remarks
        Sampler units are pipelines for retrieving texture data for rendering onto
        your objects in the world. 
    */
	class CM_EXPORT SamplerState
    {
    public:

		static SamplerState EMPTY;

        /** Texture addressing modes - default is TAM_WRAP.
        @note
            These settings are relevant in both the fixed-function and the
            programmable pipeline.
        */
        enum TextureAddressingMode
        {
            /// Texture wraps at values over 1.0
            TAM_WRAP,
            /// Texture mirrors (flips) at joins over 1.0
            TAM_MIRROR,
            /// Texture clamps at 1.0
            TAM_CLAMP,
            /// Texture coordinates outside the range [0.0, 1.0] are set to the border colour
            TAM_BORDER
        };

		/** Texture addressing mode for each texture coordinate. */
		struct UVWAddressingMode
		{
			TextureAddressingMode u, v, w;
		};

        /** Enum identifying the frame indexes for faces of a cube map (not the composite 3D type.
        */
        enum TextureCubeFace
        {
            CUBE_FRONT = 0,
            CUBE_BACK = 1,
            CUBE_LEFT = 2,
            CUBE_RIGHT = 3,
            CUBE_UP = 4,
            CUBE_DOWN = 5
        };

        /** Default constructor.
        */
        SamplerState();

        SamplerState(const SamplerState& oth );

        SamplerState & operator = ( const SamplerState& oth );

        /** Default destructor.
        */
        ~SamplerState();

		/** The type of unit to bind the texture settings to. */
		enum BindingType
		{
			/** Regular fragment processing unit - the default. */
			BT_FRAGMENT = 0,
			/** Vertex processing unit - indicates this unit will be used for 
				a vertex texture fetch.
			*/
			BT_VERTEX = 1
		};

		/** Sets the type of unit these texture settings should be bound to. 
		@remarks
			Some render systems, when implementing vertex texture fetch, separate
			the binding of textures for use in the vertex program versus those
			used in fragment programs. This setting allows you to target the
			vertex processing unit with a texture binding, in those cases. For
			rendersystems which have a unified binding for the vertex and fragment
			units, this setting makes no difference.
		*/
		void setBindingType(BindingType bt);

		/** Gets the type of unit these texture settings should be bound to.  
		*/
		BindingType getBindingType(void) const;

		/// @copydoc Texture::setHardwareGammaEnabled
		void setHardwareGammaEnabled(bool enabled);
		/// @copydoc Texture::isHardwareGammaEnabled
		bool isHardwareGammaEnabled() const;

        /** Gets the texture addressing mode for a given coordinate, 
		 	i.e. what happens at uv values above 1.0.
        @note
        	The default is TAM_WRAP i.e. the texture repeats over values of 1.0.
        */
        const UVWAddressingMode& getTextureAddressingMode(void) const;

        /** Sets the texture addressing mode, i.e. what happens at uv values above 1.0.
        @note
        The default is TAM_WRAP i.e. the texture repeats over values of 1.0.
		@note This is a shortcut method which sets the addressing mode for all
			coordinates at once; you can also call the more specific method
			to set the addressing mode per coordinate.
        @note
        This applies for both the fixed-function and programmable pipelines.
        */
        void setTextureAddressingMode( TextureAddressingMode tam);

        /** Sets the texture addressing mode, i.e. what happens at uv values above 1.0.
        @note
        The default is TAM_WRAP i.e. the texture repeats over values of 1.0.
        @note
        This applies for both the fixed-function and programmable pipelines.
		*/
        void setTextureAddressingMode( TextureAddressingMode u, 
			TextureAddressingMode v, TextureAddressingMode w);

        /** Sets the texture addressing mode, i.e. what happens at uv values above 1.0.
        @note
        The default is TAM_WRAP i.e. the texture repeats over values of 1.0.
        @note
        This applies for both the fixed-function and programmable pipelines.
		*/
        void setTextureAddressingMode( const UVWAddressingMode& uvw);

        /** Set the texture filtering for this unit, using the simplified interface.
        @remarks
            You also have the option of specifying the minification, magnification
            and mip filter individually if you want more control over filtering
            options. See the alternative setTextureFiltering methods for details.
        @note
        This option applies in both the fixed function and the programmable pipeline.
        @param filterType The high-level filter type to use.
        */
        void setTextureFiltering(TextureFilterOptions filterType);
        /** Set a single filtering option on this texture unit. 
        @params ftype The filtering type to set
        @params opts The filtering option to set
        */
        void setTextureFiltering(FilterType ftype, FilterOptions opts);
        /** Set a the detailed filtering options on this texture unit. 
        @params minFilter The filtering to use when reducing the size of the texture. 
            Can be FO_POINT, FO_LINEAR or FO_ANISOTROPIC
        @params magFilter The filtering to use when increasing the size of the texture
            Can be FO_POINT, FO_LINEAR or FO_ANISOTROPIC
        @params mipFilter The filtering to use between mip levels
            Can be FO_NONE (turns off mipmapping), FO_POINT or FO_LINEAR (trilinear filtering)
        */
        void setTextureFiltering(FilterOptions minFilter, FilterOptions magFilter, FilterOptions mipFilter);
        // get the texture filtering for the given type
        FilterOptions getTextureFiltering(FilterType ftpye) const;

        /** Sets the anisotropy level to be used for this texture level.
        @par maxAniso The maximal anisotropy level, should be between 2 and the maximum supported by hardware (1 is the default, ie. no anisotrophy).
        @note
        This option applies in both the fixed function and the programmable pipeline.
        */
        void setTextureAnisotropy(unsigned int maxAniso);
        // get this layer texture anisotropy level
        unsigned int getTextureAnisotropy() const;

		/** Sets the bias value applied to the mipmap calculation.
		@remarks
			You can alter the mipmap calculation by biasing the result with a 
			single floating point value. After the mip level has been calculated,
			this bias value is added to the result to give the final mip level.
			Lower mip levels are larger (higher detail), so a negative bias will
			force the larger mip levels to be used, and a positive bias
			will cause smaller mip levels to be used. The bias values are in 
			mip levels, so a -1 bias will force mip levels one larger than by the
			default calculation.
		@param bias The bias value as described above, can be positive or negative.
		*/
		void setTextureMipmapBias(float bias) { mMipmapBias = bias; }
		/** Gets the bias value applied to the mipmap calculation.
		@see TextureUnitState::setTextureMipmapBias
		*/
		float getTextureMipmapBias(void) const { return mMipmapBias; }
protected:
        UVWAddressingMode mAddressMode;

		bool mHwGamma;

        /// Texture filtering - minification
        FilterOptions mMinFilter;
        /// Texture filtering - magnification
        FilterOptions mMagFilter;
        /// Texture filtering - mipmapping
        FilterOptions mMipFilter;
        ///Texture anisotropy
        unsigned int mMaxAniso;
		/// Mipmap bias (always float, not float)
		float mMipmapBias;

		/// Binding type (fragment or vertex pipeline)
		BindingType mBindingType;
    };

	/** @} */
	/** @} */

}