//__________________________ Banshee Project - A modern game development toolkit _________________________________//
//_____________________________________ www.banshee-project.com __________________________________________________//
//________________________ Copyright (c) 2014 Marko Pintera. All rights reserved. ________________________________//
#include "BsInputConfiguration.h"

namespace BansheeEngine
{
	Map<String, UINT32> VirtualButton::UniqueButtonIds;
	UINT32 VirtualButton::NextButtonId = 0;

	Map<String, UINT32> VirtualAxis::UniqueAxisIds;
	UINT32 VirtualAxis::NextAxisId = 0;

	VIRTUAL_BUTTON_DESC::VIRTUAL_BUTTON_DESC()
		:buttonCode(BC_0), modifiers(VButtonModifier::None), repeatable(false)
	{ }

	VIRTUAL_BUTTON_DESC::VIRTUAL_BUTTON_DESC(ButtonCode buttonCode, VButtonModifier modifiers, bool repeatable)
		:buttonCode(buttonCode), modifiers(modifiers), repeatable(repeatable)
	{ }

	VIRTUAL_AXIS_DESC::VIRTUAL_AXIS_DESC()
		: type((UINT32)InputAxis::MouseX), deadZone(0.0001f), sensitivity(1.0f), invert(false)
	{ }

	VIRTUAL_AXIS_DESC::VIRTUAL_AXIS_DESC(UINT32 type, float deadZone, float sensitivity, bool invert)
		:type(type), deadZone(deadZone), sensitivity(sensitivity), invert(invert)
	{ }

	VirtualButton::VirtualButton()
		:buttonIdentifier(0)
	{ }

	VirtualButton::VirtualButton(const String& name)
	{
		auto findIter = UniqueButtonIds.find(name);

		if(findIter != UniqueButtonIds.end())
			buttonIdentifier = findIter->second;
		else
		{
			buttonIdentifier = NextButtonId;
			UniqueButtonIds[name] = NextButtonId++;
		}
	}

	VirtualAxis::VirtualAxis()
		:axisIdentifier(0)
	{ }

	VirtualAxis::VirtualAxis(const String& name)
	{
		auto findIter = UniqueAxisIds.find(name);

		if (findIter != UniqueAxisIds.end())
			axisIdentifier = findIter->second;
		else
		{
			axisIdentifier = NextAxisId;
			UniqueAxisIds[name] = NextAxisId++;
		}
	}

	InputConfiguration::InputConfiguration()
		:mRepeatInterval(300)
	{ }

	void InputConfiguration::registerButton(const String& name, ButtonCode buttonCode, VButtonModifier modifiers, bool repeatable)
	{
		Vector<VirtualButtonData>& btnData = mButtons[buttonCode & 0x0000FFFF];

		INT32 idx = -1;
		for(UINT32 i = 0; i < (UINT32)btnData.size(); i++)
		{
			if(btnData[i].desc.modifiers == modifiers)
			{
				idx = (INT32)i;
				break;
			}
		}

		if(idx == -1)
		{
			idx = (INT32)btnData.size();
			btnData.push_back(VirtualButtonData());
		}

		VirtualButtonData& btn = btnData[idx];
		btn.name = name;
		btn.desc = VIRTUAL_BUTTON_DESC(buttonCode, modifiers, repeatable);
		btn.button = VirtualButton(name);
	}

	void InputConfiguration::unregisterButton(const String& name)
	{
		Vector<UINT32> toRemove;

		for(UINT32 i = 0; i < BC_Count; i++)
		{
			for(UINT32 j = 0; j < (UINT32)mButtons[i].size(); j++)
			{
				if(mButtons[i][j].name == name)
					toRemove.push_back(j);
			}

			UINT32 numRemoved = 0;
			for(auto& toRemoveIdx : toRemove)
			{
				mButtons[i].erase(mButtons[i].begin() + toRemoveIdx - numRemoved);

				numRemoved++;
			}

			toRemove.clear();
		}
	}

	void InputConfiguration::registerAxis(const String& name, const VIRTUAL_AXIS_DESC& desc)
	{
		VirtualAxis axis(name);

		if (axis.axisIdentifier >= (UINT32)mAxes.size())
			mAxes.resize(axis.axisIdentifier + 1);

		mAxes[axis.axisIdentifier].name = name;
		mAxes[axis.axisIdentifier].desc = desc;
		mAxes[axis.axisIdentifier].axis = axis;
	}

	void InputConfiguration::unregisterAxis(const String& name)
	{
		for (UINT32 i = 0; i < (UINT32)mAxes.size(); i++)
		{
			if (mAxes[i].name == name)
			{
				mAxes.erase(mAxes.begin() + i);
				i--;
			}
		}
	}

	bool InputConfiguration::_getButton(ButtonCode code, UINT32 modifiers, VirtualButton& btn, VIRTUAL_BUTTON_DESC& btnDesc) const
	{
		const Vector<VirtualButtonData>& btnData = mButtons[code & 0x0000FFFF];

		for(UINT32 i = 0; i < (UINT32)btnData.size(); i++)
		{
			if((((UINT32)btnData[i].desc.modifiers) & modifiers) == ((UINT32)btnData[i].desc.modifiers))
			{
				btn = btnData[i].button;
				btnDesc = btnData[i].desc;
				return true;
			}
		}

		return false;
	}

	bool InputConfiguration::_getAxis(const VirtualAxis& axis, VIRTUAL_AXIS_DESC& axisDesc) const
	{
		if (axis.axisIdentifier >= (UINT32)mAxes.size())
			return false;

		axisDesc = mAxes[axis.axisIdentifier].desc;
		return true;
	}
}