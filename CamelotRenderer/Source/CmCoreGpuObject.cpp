#include "CmCoreGpuObject.h"
#include "CmRenderSystem.h"
#include "CmCoreGpuObjectManager.h"
#include "CmDebug.h"

namespace CamelotEngine
{
	CM_STATIC_THREAD_SYNCHRONISER_CLASS_INSTANCE(mCoreGpuObjectLoadedCondition, CoreGpuObject)
	CM_STATIC_MUTEX_CLASS_INSTANCE(mCoreGpuObjectLoadedMutex, CoreGpuObject)

	CoreGpuObject::CoreGpuObject()
		: mFlags(0), mInternalID(0)
	{
		mInternalID = CoreGpuObjectManager::instance().registerObject(this);
	}

	CoreGpuObject::~CoreGpuObject() 
	{
		if(isInitialized())
		{
			// Object must be released with destroy() otherwise engine can still try to use it, even if it was destructed
			// (e.g. if an object has one of its methods queued in a command queue, and is destructed, you will be accessing invalid memory)
			CM_EXCEPT(InternalErrorException, "Destructor called but object is not destroyed. This will result in nasty issues.");
		}

#if CM_DEBUG_MODE
		if(!mThis.expired())
		{
			CM_EXCEPT(InternalErrorException, "Shared pointer to this object still has active references but " \
				"the object is being deleted? You shouldn't delete CoreGpuObjects manually.");
		}
#endif

		CoreGpuObjectManager::instance().unregisterObject(this);
	}

	void CoreGpuObject::destroy()
	{
		setScheduledToBeDeleted(true);
		CoreGpuObjectManager::instance().registerObjectToDestroy(mThis.lock());

		queueGpuCommand(mThis.lock(), &CoreGpuObject::destroy_internal);
	}

	void CoreGpuObject::destroy_internal()
	{
#if CM_DEBUG_MODE
		if(!isInitialized())
		{
			CoreGpuObjectManager::instance().unregisterObjectToDestroy(mThis.lock());
			CM_EXCEPT(InternalErrorException, "Trying to destroy an object that is already destroyed (or it never was initialized).");
		}
#endif

		setIsInitialized(false);

		CoreGpuObjectManager::instance().unregisterObjectToDestroy(mThis.lock());
	}

	void CoreGpuObject::initialize()
	{
#if CM_DEBUG_MODE
		if(isInitialized() || isScheduledToBeInitialized())
			CM_EXCEPT(InternalErrorException, "Trying to initialize an object that is already initialized.");
#endif

		setScheduledToBeInitialized(true);

		queueGpuCommand(mThis.lock(), &CoreGpuObject::initialize_internal);
	}

	void CoreGpuObject::initialize_internal()
	{
		{
			CM_LOCK_MUTEX(mCoreGpuObjectLoadedMutex);
			setIsInitialized(true);
		}	

		setScheduledToBeInitialized(false);

		CM_THREAD_NOTIFY_ALL(mCoreGpuObjectLoadedCondition);
	}

	void CoreGpuObject::waitUntilInitialized()
	{
#if CM_DEBUG_MODE
		if(CM_THREAD_CURRENT_ID == RenderSystem::instancePtr()->getRenderThreadId())
			CM_EXCEPT(InternalErrorException, "You cannot call this method on the render thread. It will cause a deadlock!");
#endif

		if(!isInitialized())
		{
			CM_LOCK_MUTEX_NAMED(mCoreGpuObjectLoadedMutex, lock);
			while(!isInitialized())
			{
				if(!isScheduledToBeInitialized())
					CM_EXCEPT(InternalErrorException, "Attempting to wait until initialization finishes but object is not scheduled to be initialized.");

				CM_THREAD_WAIT(mCoreGpuObjectLoadedCondition, mCoreGpuObjectLoadedMutex, lock);
			}
		}
	}

	void CoreGpuObject::setThisPtr(std::shared_ptr<CoreGpuObject> ptrThis)
	{
		mThis = ptrThis;
	}

	void CoreGpuObject::_deleteDelayed(CoreGpuObject* obj)
	{
		assert(obj != nullptr);

		// This method usually gets called automatically by the shared pointer when all references are released. The process:
		// - If the object wasn't initialized delete it right away
		// - Otherwise:
		//  - We re-create the reference to the object by setting mThis pointer
		//  - We queue the object to be destroyed so all of its GPU resources may be released on the render thread
		//    - destroy() makes sure it keeps a reference of mThis so object isn't deleted
		//    - Once the destroy() finishes the reference is removed and the default shared_ptr deleter is called

#if CM_DEBUG_MODE
		if(obj->isScheduledToBeInitialized())
		{
			CM_EXCEPT(InternalErrorException, "Object scheduled to be initialized, yet it's being deleted. " \
				"By design objects queued in the command queue should always have a reference count >= 1, therefore never be deleted " \
				"while still in the queue.");
		}
#endif

		if(obj->isInitialized())
		{
			std::shared_ptr<CoreGpuObject> thisPtr(obj);
			obj->setThisPtr(thisPtr);
			obj->destroy();
		}
		else
		{
			delete obj;
		}
	}

	void CoreGpuObject::queueGpuCommand(std::shared_ptr<CoreGpuObject> obj, boost::function<void(CoreGpuObject*)> func)
	{
		// We call another internal method and go through an additional layer of abstraction in order to keep an active
		// reference to the obj (saved in the bound function).
		// We could have called the function directly using "this" pointer but then we couldn't have used a shared_ptr for the object,
		// in which case there is a possibility that the object would be released and deleted while still being in the command queue.
		RenderSystem::instancePtr()->queueCommand(boost::bind(&CoreGpuObject::executeGpuCommand, obj, func));
	}

	AsyncOp CoreGpuObject::queueReturnGpuCommand(std::shared_ptr<CoreGpuObject> obj, boost::function<void(CoreGpuObject*, AsyncOp&)> func)
	{
		// See queueGpuCommand
		return RenderSystem::instancePtr()->queueReturnCommand(boost::bind(&CoreGpuObject::executeReturnGpuCommand, obj, func, _1));
	}

	void CoreGpuObject::executeGpuCommand(std::shared_ptr<CoreGpuObject> obj, boost::function<void(CoreGpuObject*)> func)
	{
		func(obj.get());
	}

	void CoreGpuObject::executeReturnGpuCommand(std::shared_ptr<CoreGpuObject> obj, boost::function<void(CoreGpuObject*, AsyncOp&)> func, AsyncOp& op)
	{
		func(obj.get(), op);
	}
}