/**************************************************************
 * 
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 * 
 *************************************************************/



#include "componentmodule.hxx"
#include <tools/resmgr.hxx>
#ifndef _SOLAR_HRC
#include <svl/solar.hrc>
#endif
#include <comphelper/sequence.hxx>
#include <tools/debug.hxx>

#define ENTER_MOD_METHOD()	\
	::osl::MutexGuard aGuard(s_aMutex);	\
	ensureImpl()
	
//.........................................................................
namespace COMPMOD_NAMESPACE
{
//.........................................................................

	using namespace ::com::sun::star::uno;
	using namespace ::com::sun::star::lang;
	using namespace ::com::sun::star::registry;
	using namespace ::comphelper;
	using namespace ::cppu;

	//=========================================================================
	//= OModuleImpl
	//=========================================================================
	/** implementation for <type>OModule</type>. not threadsafe, has to be guarded by it's owner
	*/
	class OModuleImpl
	{
		ResMgr*		m_pRessources;
		sal_Bool	m_bInitialized;
		ByteString	m_sFilePrefix;

	public:
		/// ctor
		OModuleImpl();
		~OModuleImpl();

		/// get the manager for the ressources of the module
		ResMgr*	getResManager();
		void	setResourceFilePrefix(const ::rtl::OString& _rPrefix) { m_sFilePrefix = _rPrefix; }
	};

	//-------------------------------------------------------------------------
	OModuleImpl::OModuleImpl()
		:m_pRessources(NULL)
		,m_bInitialized(sal_False)
	{
	}

	//-------------------------------------------------------------------------
	OModuleImpl::~OModuleImpl()
	{
		if (m_pRessources)
			delete m_pRessources;
	}

	//-------------------------------------------------------------------------
	ResMgr*	OModuleImpl::getResManager()
	{
		// note that this method is not threadsafe, which counts for the whole class !
		if (!m_pRessources && !m_bInitialized)
		{
			DBG_ASSERT(m_sFilePrefix.Len(), "OModuleImpl::getResManager: no resource file prefix!");
			// create a manager with a fixed prefix
			ByteString aMgrName = m_sFilePrefix;

			m_pRessources = ResMgr::CreateResMgr(aMgrName.GetBuffer());
			DBG_ASSERT(m_pRessources, 
					(ByteString("OModuleImpl::getResManager: could not create the resource manager (file name: ")
				+=	aMgrName
				+=	ByteString(")!")).GetBuffer());

			m_bInitialized = sal_True;
		}
		return m_pRessources;
	}

	//=========================================================================
	//= OModule
	//=========================================================================
	::osl::Mutex	OModule::s_aMutex;
	sal_Int32		OModule::s_nClients = 0;
	OModuleImpl*	OModule::s_pImpl = NULL;
	::rtl::OString	OModule::s_sResPrefix;
	//-------------------------------------------------------------------------
	ResMgr*	OModule::getResManager()
	{
		ENTER_MOD_METHOD();
		return s_pImpl->getResManager();
	}

	//-------------------------------------------------------------------------
	void OModule::setResourceFilePrefix(const ::rtl::OString& _rPrefix)
	{
		::osl::MutexGuard aGuard(s_aMutex);
		s_sResPrefix = _rPrefix;
		if (s_pImpl)
			s_pImpl->setResourceFilePrefix(_rPrefix);
	}

	//-------------------------------------------------------------------------
	void OModule::registerClient()
	{
		::osl::MutexGuard aGuard(s_aMutex);
		++s_nClients;
	}

	//-------------------------------------------------------------------------
	void OModule::revokeClient()
	{
		::osl::MutexGuard aGuard(s_aMutex);
		if (!--s_nClients && s_pImpl)
		{
			delete s_pImpl;
			s_pImpl = NULL;
		}
	}

	//-------------------------------------------------------------------------
	void OModule::ensureImpl()
	{
		if (s_pImpl)
			return;
		s_pImpl = new OModuleImpl();
		s_pImpl->setResourceFilePrefix(s_sResPrefix);
	}

	//--------------------------------------------------------------------------
	//- registration helper
	//--------------------------------------------------------------------------

	Sequence< ::rtl::OUString >*				OModule::s_pImplementationNames = NULL;
	Sequence< Sequence< ::rtl::OUString > >*	OModule::s_pSupportedServices = NULL;
	Sequence< sal_Int64 >*						OModule::s_pCreationFunctionPointers = NULL;
	Sequence< sal_Int64 >*						OModule::s_pFactoryFunctionPointers = NULL;

	//--------------------------------------------------------------------------
	void OModule::registerComponent(
		const ::rtl::OUString& _rImplementationName,
		const Sequence< ::rtl::OUString >& _rServiceNames,
		ComponentInstantiation _pCreateFunction,
		FactoryInstantiation _pFactoryFunction)
	{
		if (!s_pImplementationNames)
		{
			OSL_ENSURE(!s_pSupportedServices && !s_pCreationFunctionPointers && !s_pFactoryFunctionPointers,
				"OModule::registerComponent : inconsistent state (the pointers (1)) !");
			s_pImplementationNames = new Sequence< ::rtl::OUString >;
			s_pSupportedServices = new Sequence< Sequence< ::rtl::OUString > >;
			s_pCreationFunctionPointers = new Sequence< sal_Int64 >;
			s_pFactoryFunctionPointers = new Sequence< sal_Int64 >;
		}
		OSL_ENSURE(s_pImplementationNames && s_pSupportedServices && s_pCreationFunctionPointers && s_pFactoryFunctionPointers,
			"OModule::registerComponent : inconsistent state (the pointers (2)) !");

		OSL_ENSURE(	(s_pImplementationNames->getLength() == s_pSupportedServices->getLength())
					&&	(s_pImplementationNames->getLength() == s_pCreationFunctionPointers->getLength())
					&&	(s_pImplementationNames->getLength() == s_pFactoryFunctionPointers->getLength()),
			"OModule::registerComponent : inconsistent state !");

		sal_Int32 nOldLen = s_pImplementationNames->getLength();
		s_pImplementationNames->realloc(nOldLen + 1);
		s_pSupportedServices->realloc(nOldLen + 1);
		s_pCreationFunctionPointers->realloc(nOldLen + 1);
		s_pFactoryFunctionPointers->realloc(nOldLen + 1);

		s_pImplementationNames->getArray()[nOldLen] = _rImplementationName;
		s_pSupportedServices->getArray()[nOldLen] = _rServiceNames;
		s_pCreationFunctionPointers->getArray()[nOldLen] = reinterpret_cast<sal_Int64>(_pCreateFunction);
		s_pFactoryFunctionPointers->getArray()[nOldLen] = reinterpret_cast<sal_Int64>(_pFactoryFunction);
	}

	//--------------------------------------------------------------------------
	void OModule::revokeComponent(const ::rtl::OUString& _rImplementationName)
	{
		if (!s_pImplementationNames)
		{
			OSL_ASSERT("OModule::revokeComponent : have no class infos ! Are you sure called this method at the right time ?");
			return;
		}
		OSL_ENSURE(s_pImplementationNames && s_pSupportedServices && s_pCreationFunctionPointers && s_pFactoryFunctionPointers,
			"OModule::revokeComponent : inconsistent state (the pointers) !");
		OSL_ENSURE(	(s_pImplementationNames->getLength() == s_pSupportedServices->getLength())
					&&	(s_pImplementationNames->getLength() == s_pCreationFunctionPointers->getLength())
					&&	(s_pImplementationNames->getLength() == s_pFactoryFunctionPointers->getLength()),
			"OModule::revokeComponent : inconsistent state !");

		sal_Int32 nLen = s_pImplementationNames->getLength();
		const ::rtl::OUString* pImplNames = s_pImplementationNames->getConstArray();
		for (sal_Int32 i=0; i<nLen; ++i, ++pImplNames)
		{
			if (pImplNames->equals(_rImplementationName))
			{
				removeElementAt(*s_pImplementationNames, i);
				removeElementAt(*s_pSupportedServices, i);
				removeElementAt(*s_pCreationFunctionPointers, i);
				removeElementAt(*s_pFactoryFunctionPointers, i);
				break;
			}
		}

		if (s_pImplementationNames->getLength() == 0)
		{
			delete s_pImplementationNames; s_pImplementationNames = NULL;
			delete s_pSupportedServices; s_pSupportedServices = NULL;
			delete s_pCreationFunctionPointers; s_pCreationFunctionPointers = NULL;
			delete s_pFactoryFunctionPointers; s_pFactoryFunctionPointers = NULL;
		}
	}

	//--------------------------------------------------------------------------
	Reference< XInterface > OModule::getComponentFactory(
		const ::rtl::OUString& _rImplementationName,
		const Reference< XMultiServiceFactory >& _rxServiceManager)
	{
		OSL_ENSURE(_rxServiceManager.is(), "OModule::getComponentFactory : invalid argument (service manager) !");
		OSL_ENSURE(_rImplementationName.getLength(), "OModule::getComponentFactory : invalid argument (implementation name) !");

		if (!s_pImplementationNames)
		{
			OSL_ASSERT("OModule::getComponentFactory : have no class infos ! Are you sure called this method at the right time ?");
			return NULL;
		}
		OSL_ENSURE(s_pImplementationNames && s_pSupportedServices && s_pCreationFunctionPointers && s_pFactoryFunctionPointers,
			"OModule::getComponentFactory : inconsistent state (the pointers) !");
		OSL_ENSURE(	(s_pImplementationNames->getLength() == s_pSupportedServices->getLength())
					&&	(s_pImplementationNames->getLength() == s_pCreationFunctionPointers->getLength())
					&&	(s_pImplementationNames->getLength() == s_pFactoryFunctionPointers->getLength()),
			"OModule::getComponentFactory : inconsistent state !");


		Reference< XInterface > xReturn;


		sal_Int32 nLen = s_pImplementationNames->getLength();
		const ::rtl::OUString* pImplName = s_pImplementationNames->getConstArray();
		const Sequence< ::rtl::OUString >* pServices = s_pSupportedServices->getConstArray();
		const sal_Int64* pComponentFunction = s_pCreationFunctionPointers->getConstArray();
		const sal_Int64* pFactoryFunction = s_pFactoryFunctionPointers->getConstArray();

		for (sal_Int32 i=0; i<nLen; ++i, ++pImplName, ++pServices, ++pComponentFunction, ++pFactoryFunction)
		{
			if (pImplName->equals(_rImplementationName))
			{
				const FactoryInstantiation FactoryInstantiationFunction = reinterpret_cast<const FactoryInstantiation>(*pFactoryFunction);
				const ComponentInstantiation ComponentInstantiationFunction = reinterpret_cast<const ComponentInstantiation>(*pComponentFunction);

				xReturn = FactoryInstantiationFunction( _rxServiceManager, *pImplName, ComponentInstantiationFunction, *pServices, NULL);
				if (xReturn.is())
				{
					xReturn->acquire();
					return xReturn.get();
				}
			}
		}

		return NULL;
	}


//.........................................................................
}	// namespace COMPMOD_NAMESPACE
//.........................................................................

