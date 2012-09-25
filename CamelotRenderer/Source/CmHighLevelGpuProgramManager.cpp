/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

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
#include "CmHighLevelGpuProgramManager.h"

namespace CamelotEngine {

	String sNullLang = "null";
	class NullProgram : public HighLevelGpuProgram
	{
	protected:
		/** Internal load implementation, must be implemented by subclasses.
		*/
		void loadFromSource(void) {}
		/** Internal method for creating an appropriate low-level program from this
		high-level program, must be implemented by subclasses. */
		void createLowLevelImpl(void) {}
		/// Internal unload implementation, must be implemented by subclasses
		void unloadHighLevelImpl(void) {}
		/// Populate the passed parameters with name->index map, must be overridden
		void populateParameterNames(GpuProgramParametersSharedPtr params)
		{
			// Skip the normal implementation
			// Ensure we don't complain about missing parameter names
			params->setIgnoreMissingParams(true);

		}
		void buildConstantDefinitions() const
		{
			// do nothing
		}
	public:
		NullProgram()
			: HighLevelGpuProgram(){}
		~NullProgram() {}
		/// Overridden from GpuProgram - never supported
		bool isSupported(void) const { return false; }
		/// Overridden from GpuProgram
		const String& getLanguage(void) const { return sNullLang; }

		/// Overridden from StringInterface
		bool setParameter(const String& name, const String& value)
		{
			// always silently ignore all parameters so as not to report errors on
			// unsupported platforms
			return true;
		}

	};
	class NullProgramFactory : public HighLevelGpuProgramFactory
	{
	public:
		NullProgramFactory() {}
		~NullProgramFactory() {}
		/// Get the name of the language this factory creates programs for
		const String& getLanguage(void) const 
		{ 
			return sNullLang;
		}
		HighLevelGpuProgram* create(const String& source, const String& entryPoint, GpuProgramProfile profile)
		{
			return new NullProgram();
		}
		void destroy(HighLevelGpuProgram* prog)
		{
			delete prog;
		}

	};
	//-----------------------------------------------------------------------
	HighLevelGpuProgramManager::HighLevelGpuProgramManager()
	{
		mNullFactory = new NullProgramFactory();
		addFactory(mNullFactory);
	}
	//-----------------------------------------------------------------------
	HighLevelGpuProgramManager::~HighLevelGpuProgramManager()
	{
		delete mNullFactory;
	}
    //---------------------------------------------------------------------------
	void HighLevelGpuProgramManager::addFactory(HighLevelGpuProgramFactory* factory)
	{
		// deliberately allow later plugins to override earlier ones
		mFactories[factory->getLanguage()] = factory;
	}
    //---------------------------------------------------------------------------
    void HighLevelGpuProgramManager::removeFactory(HighLevelGpuProgramFactory* factory)
    {
        // Remove only if equal to registered one, since it might overridden
        // by other plugins
        FactoryMap::iterator it = mFactories.find(factory->getLanguage());
        if (it != mFactories.end() && it->second == factory)
        {
            mFactories.erase(it);
        }
    }
    //---------------------------------------------------------------------------
	HighLevelGpuProgramFactory* HighLevelGpuProgramManager::getFactory(const String& language)
	{
		FactoryMap::iterator i = mFactories.find(language);

		if (i == mFactories.end())
		{
			// use the null factory to create programs that will never be supported
			i = mFactories.find(sNullLang);
		}
		return i->second;
	}
	//---------------------------------------------------------------------
	bool HighLevelGpuProgramManager::isLanguageSupported(const String& lang)
	{
		FactoryMap::iterator i = mFactories.find(lang);

		return i != mFactories.end();

	}
    //---------------------------------------------------------------------------
    HighLevelGpuProgramPtr HighLevelGpuProgramManager::createProgram(const String& source, const String& entryPoint, const String& language, GpuProgramType gptype, GpuProgramProfile profile)
    {
        HighLevelGpuProgramPtr ret = HighLevelGpuProgramPtr(getFactory(language)->create(source, entryPoint, profile));

        HighLevelGpuProgramPtr prg = ret;
        prg->setType(gptype);
        prg->setSyntaxCode(language);

        return prg;
    }
    //---------------------------------------------------------------------------
    HighLevelGpuProgramFactory::~HighLevelGpuProgramFactory() 
    {
    }
}