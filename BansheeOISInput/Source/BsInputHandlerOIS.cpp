//__________________________ Banshee Project - A modern game development toolkit _________________________________//
//_____________________________________ www.banshee-project.com __________________________________________________//
//________________________ Copyright (c) 2014 Marko Pintera. All rights reserved. ________________________________//
#include "BsInputHandlerOIS.h"
#include "BsVector2I.h"
#include "OIS/OISException.h"
#include "BsRenderWindow.h"
#include "BsTime.h"
#include "BsMath.h"
#include "BsDebug.h"

namespace BansheeEngine
{
	const UINT32 InputHandlerOIS::MOUSE_DPI = 800;
	const float InputHandlerOIS::MOUSE_MAX = 0.05f;
	const float InputHandlerOIS::MOUSE_MAX_TIME = 0.020f; // 20 ms
	const float InputHandlerOIS::MOUSE_MAX_SAMPLING_RATE = 0.006f; // 6ms

	GamepadEventListener::GamepadEventListener(InputHandlerOIS* parentHandler, UINT32 joystickIdx)
		:mParentHandler(parentHandler), mGamepadIdx(joystickIdx)
	{ }

	bool GamepadEventListener::buttonPressed(const OIS::JoyStickEvent& arg, int button)
	{
		ButtonCode bc = InputHandlerOIS::gamepadButtonToButtonCode(button);

		// Note: No timestamps for gamepad buttons, but they shouldn't be used for anything anyway
		mParentHandler->onButtonDown(mGamepadIdx, bc, 0);
		return true;
	}

	bool GamepadEventListener::buttonReleased(const OIS::JoyStickEvent& arg, int button)
	{
		ButtonCode bc = InputHandlerOIS::gamepadButtonToButtonCode(button);

		// Note: No timestamps for gamepad buttons, but they shouldn't be used for anything anyway
		mParentHandler->onButtonUp(mGamepadIdx, bc, 0);
		return true;
	}

	bool GamepadEventListener::axisMoved(const OIS::JoyStickEvent& arg, int axis)
	{
		// Move axis values into [-1.0f, 1.0f] range
		float axisRange = Math::abs((float)OIS::JoyStick::MAX_AXIS) + Math::abs((float)OIS::JoyStick::MIN_AXIS);

		INT32 axisRel = arg.state.mAxes[axis].rel;
		INT32 axisAbs = arg.state.mAxes[axis].abs;

		RawAxisState axisState;
		axisState.rel = ((axisRel + Math::abs((float)OIS::JoyStick::MIN_AXIS)) / axisRange) * 2.0f - 1.0f;
		axisState.abs = ((axisAbs + Math::abs((float)OIS::JoyStick::MIN_AXIS)) / axisRange) * 2.0f - 1.0f;

		mParentHandler->onAxisMoved(mGamepadIdx, axisState, (UINT32)axis);

		return true;
	}

	InputHandlerOIS::InputHandlerOIS(unsigned int hWnd)
		:mInputManager(nullptr), mKeyboard(nullptr), mMouse(nullptr), mTimestampClockOffset(0),
		mLastMouseUpdateFrame(0), mMouseSampleCounter(0.0f)
	{
		mMouseSampleAccumulator[0] = 0;
		mMouseSampleAccumulator[1] = 0;
		mTotalMouseSamplingTime[0] = 1.0f / 125.0f; // Use 125Hz as initial pooling rate for mice
		mTotalMouseSamplingTime[1] = 1.0f / 125.0f;
		mTotalMouseNumSamples[0] = 1;
		mTotalMouseNumSamples[1] = 1;
		mMouseSmoothedAxis[0] = 0.0f;
		mMouseSmoothedAxis[1] = 0.0f;
		mMouseZeroTime[0] = 0.0f;
		mMouseZeroTime[1] = 0.0f;

		OIS::ParamList pl;
		std::ostringstream windowHndStr;
		windowHndStr << hWnd;
		pl.insert(std::make_pair(std::string("WINDOW"), windowHndStr.str()));

#if defined BS_PLATFORM == BS_PLATFORM_WIN32
		pl.insert(std::make_pair(std::string("w32_mouse"), std::string("DISCL_FOREGROUND" )));
		pl.insert(std::make_pair(std::string("w32_mouse"), std::string("DISCL_NONEXCLUSIVE")));
		pl.insert(std::make_pair(std::string("w32_keyboard"), std::string("DISCL_FOREGROUND")));
		pl.insert(std::make_pair(std::string("w32_keyboard"), std::string("DISCL_NONEXCLUSIVE")));
#elif defined BS_PLATFORM == BS_PLATFORM_LINUX || BS_PLATFORM == BS_PLATFORM_APPLE
		pl.insert(std::make_pair(std::string("x11_mouse_grab"), std::string("false")));
		pl.insert(std::make_pair(std::string("x11_mouse_hide"), std::string("false")));
		pl.insert(std::make_pair(std::string("x11_keyboard_grab"), std::string("false")));
		pl.insert(std::make_pair(std::string("XAutoRepeatOn"), std::string("true")));
#endif
		try
		{
			mInputManager = OIS::InputManager::createInputSystem(pl);
		}
		catch(OIS::Exception &e)
		{
			std::cout << e.eText << std::endl;
		}

		if (mInputManager->getNumberOfDevices(OIS::OISKeyboard) > 0)
		{
			mKeyboard = static_cast<OIS::Keyboard*>(mInputManager->createInputObject(OIS::OISKeyboard, true));
			mKeyboard->setEventCallback(this);
		}

		if (mInputManager->getNumberOfDevices(OIS::OISMouse) > 0)
		{
			mMouse = static_cast<OIS::Mouse*>(mInputManager->createInputObject(OIS::OISMouse, true));
			mMouse->setEventCallback(this);
		}

		UINT32 numGamepads = mInputManager->getNumberOfDevices(OIS::OISJoyStick);
		for (UINT32 i = 0; i < numGamepads; i++)
		{
			mGamepads.push_back(GamepadData());
			GamepadData& gamepadData = mGamepads.back();

			gamepadData.gamepad = static_cast<OIS::JoyStick*>(mInputManager->createInputObject(OIS::OISJoyStick, true));
			gamepadData.listener = bs_new<GamepadEventListener>(this, i);

			gamepadData.gamepad->setEventCallback(gamepadData.listener);
		}

		// OIS reports times since system start but we use time since program start
		mTimestampClockOffset = gTime().getStartTimeMs();
	}

	InputHandlerOIS::~InputHandlerOIS()
	{
		if(mInputManager)
		{		
			if(mMouse != nullptr)
				mInputManager->destroyInputObject(mMouse);

			if(mKeyboard != nullptr)
				mInputManager->destroyInputObject(mKeyboard);

			for (auto& gamepadData : mGamepads)
			{
				mInputManager->destroyInputObject(gamepadData.gamepad);
				bs_delete(gamepadData.listener);
			}

			OIS::InputManager::destroyInputSystem(mInputManager);
			mInputManager = nullptr;
		}
	}

	float InputHandlerOIS::smoothMouse(float value, UINT32 idx)
	{
		UINT32 sampleCount = 1;

		float deltaTime = gTime().getFrameDelta();
		if (deltaTime < 0.25f)
		{
			float secondsPerSample = mTotalMouseSamplingTime[idx] / mTotalMouseNumSamples[idx];

			if (value == 0.0f)
			{
				mMouseZeroTime[idx] += deltaTime;
				if (mMouseZeroTime[idx] < secondsPerSample)
					value = mMouseSmoothedAxis[idx] * deltaTime / secondsPerSample;
				else
					mMouseSmoothedAxis[idx] = 0;
			}
			else
			{
				mMouseZeroTime[idx] = 0;
				if (mMouseSmoothedAxis[idx] != 0)
				{
					if (deltaTime < secondsPerSample * (sampleCount + 1))
						value = value * deltaTime / (secondsPerSample * sampleCount);
					else
						sampleCount = Math::roundToInt(deltaTime / secondsPerSample);
				}

				mMouseSmoothedAxis[idx] = value / sampleCount;
			}
		}
		else
		{
			mMouseSmoothedAxis[idx] = 0.0f;
			mMouseZeroTime[idx] = 0.0f;
		}

		return value;
	}

	void InputHandlerOIS::_update()
	{
		if (mMouse != nullptr)
			mMouse->capture();

		if (mKeyboard != nullptr)
			mKeyboard->capture();

		for (auto& gamepadData : mGamepads)
		{
			gamepadData.gamepad->capture();
		}

		// Limit mouse sampling to a certain rate to avoid jitter at extremely high frame rates.
		// (As the application might request samples faster than they are produced)
		mMouseSampleCounter += gTime().getFrameDelta();
		if (mMouseSampleCounter < MOUSE_MAX_SAMPLING_RATE)
			return;

		float rawXValue = 0.0f;
		float rawYValue = 0.0f;

		// Smooth mouse axes if needed
		if (mMouseSmoothingEnabled)
		{
			rawXValue = smoothMouse((float)mMouseSampleAccumulator[0], 0);
			rawYValue = smoothMouse((float)mMouseSampleAccumulator[1], 1);
		}
		else
		{
			rawXValue = (float)mMouseSampleAccumulator[0];
			rawYValue = (float)mMouseSampleAccumulator[1];
		}

		mMouseSampleAccumulator[0] = 0;
		mMouseSampleAccumulator[1] = 0;

		// Scale by time so that we are framerate independant: rawXValue = rawXValue * (MOUSE_MAX_TIME / gTime().getFrameDelta());
		// Scale to valid [-1.0, 1.0] range: rawXValue / (MOUSE_DPI * MOUSE_MAX)
		// This is just the combination of the two:

		float axisScale = ((MOUSE_DPI * MOUSE_MAX) / MOUSE_MAX_TIME) * mMouseSampleCounter;

		RawAxisState xState;
		xState.rel = -Math::clamp(rawXValue / axisScale, -1.0f, 1.0f);
		xState.abs = xState.rel; // Abs value irrelevant for mouse

		onAxisMoved(0, xState, (UINT32)InputAxis::MouseX);

		RawAxisState yState;
		yState.rel = -Math::clamp(rawYValue / axisScale, -1.0f, 1.0f);
		yState.abs = yState.rel; // Abs value irrelevant for mouse
		
		onAxisMoved(0, yState, (UINT32)InputAxis::MouseY);

		mMouseSampleCounter = 0.0f;
	}

	void InputHandlerOIS::_inputWindowChanged(const RenderWindow& win)
	{
		unsigned long long hWnd;
		win.getCustomAttribute("WINDOW", &hWnd);

		std::string normalString = toString((UINT64)hWnd).c_str();
		mKeyboard->setCaptureContext(normalString);
		mMouse->setCaptureContext(normalString);
	}

	bool InputHandlerOIS::keyPressed(const OIS::KeyEvent &arg)
	{
		onButtonDown(0, keyCodeToButtonCode(arg.key), arg.timestamp - mTimestampClockOffset);
		return true;
	}

	bool InputHandlerOIS::keyReleased(const OIS::KeyEvent& arg)
	{
		onButtonUp(0, keyCodeToButtonCode(arg.key), arg.timestamp - mTimestampClockOffset);
		return true;
	}

	bool InputHandlerOIS::mouseMoved(const OIS::MouseEvent& arg)
	{
		mMouseSampleAccumulator[0] += arg.state.X.rel;
		mMouseSampleAccumulator[1] += arg.state.Y.rel;

		mTotalMouseNumSamples[0] += Math::roundToInt(Math::abs((float)arg.state.X.rel));
		mTotalMouseNumSamples[1] += Math::roundToInt(Math::abs((float)arg.state.Y.rel));

		// Update sample times used for determining sampling rate. But only if something was
		// actually sampled, and only if this isn't the first non-zero sample.
		if (mLastMouseUpdateFrame != gTime().getCurrentFrameNumber())
		{
			if (arg.state.X.rel != 0 && !Math::approxEquals(mMouseSmoothedAxis[0], 0.0f))
				mTotalMouseSamplingTime[0] += gTime().getFrameDelta();

			if (arg.state.Y.rel != 0 && !Math::approxEquals(mMouseSmoothedAxis[1], 0.0f))
				mTotalMouseSamplingTime[1] += gTime().getFrameDelta();

			mLastMouseUpdateFrame = gTime().getCurrentFrameNumber();
		}

		RawAxisState zState;
		zState.abs = (float)arg.state.Z.abs;
		zState.rel = (float)arg.state.Z.rel;

		onAxisMoved(0, zState, (UINT32)InputAxis::MouseZ);

		return true;
	}

	bool InputHandlerOIS::mousePressed(const OIS::MouseEvent& arg, OIS::MouseButtonID id)
	{
		onButtonDown(0, mouseButtonToButtonCode(id), arg.timestamp - mTimestampClockOffset);

		return true;
	}

	bool InputHandlerOIS::mouseReleased(const OIS::MouseEvent& arg, OIS::MouseButtonID id)
	{
		onButtonUp(0, mouseButtonToButtonCode(id), arg.timestamp - mTimestampClockOffset);

		return true;
	}

	ButtonCode InputHandlerOIS::keyCodeToButtonCode(OIS::KeyCode keyCode)
	{
		return (ButtonCode)keyCode;
	}

	ButtonCode InputHandlerOIS::mouseButtonToButtonCode(OIS::MouseButtonID mouseBtn)
	{
		return (ButtonCode)(((int)mouseBtn + BC_NumKeys) | 0x80000000);
	}

	ButtonCode InputHandlerOIS::gamepadButtonToButtonCode(INT32 joystickCode)
	{
		switch (joystickCode)
		{
		case 0:
			return BC_GAMEPAD_DPAD_UP;
		case 1:
			return BC_GAMEPAD_DPAD_DOWN;
		case 2:
			return BC_GAMEPAD_DPAD_LEFT;
		case 3:
			return BC_GAMEPAD_DPAD_RIGHT;
		case 4:
			return BC_GAMEPAD_START;
		case 5:
			return BC_GAMEPAD_BACK;
		case 6:
			return BC_GAMEPAD_LS;
		case 7:
			return BC_GAMEPAD_RS;
		case 8:
			return BC_GAMEPAD_LB;
		case 9:
			return BC_GAMEPAD_RB;
		case 10:
			return BC_GAMEPAD_BTN1;
		case 11:
			return BC_GAMEPAD_LS;
		case 12:
			return BC_GAMEPAD_A;
		case 13:
			return BC_GAMEPAD_B;
		case 14:
			return BC_GAMEPAD_X;
		case 15:
			return BC_GAMEPAD_Y;
		}

		return (ButtonCode)(BC_GAMEPAD_BTN1 + (joystickCode - 15));
	}
}