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



// MARKER(update_precomp.py): autogen include statement, do not remove
#include "precompiled_desktop.hxx"
 
#include "dp_backend.h"
#include "dp_ucb.h"
#include "rtl/uri.hxx"
#include "rtl/bootstrap.hxx"
#include "osl/file.hxx"
#include "cppuhelper/exc_hlp.hxx"
#include "comphelper/servicedecl.hxx"
#include "comphelper/unwrapargs.hxx"
#include "ucbhelper/content.hxx"
#include "com/sun/star/lang/WrappedTargetRuntimeException.hpp"
#include "com/sun/star/deployment/InvalidRemovedParameterException.hpp"
#include "com/sun/star/deployment/thePackageManagerFactory.hpp"
#include "com/sun/star/ucb/InteractiveAugmentedIOException.hpp"
#include "com/sun/star/ucb/IOErrorCode.hpp"
#include "com/sun/star/beans/StringPair.hpp"
#include "com/sun/star/sdbc/XResultSet.hpp"
#include "com/sun/star/sdbc/XRow.hpp"
#include "unotools/tempfile.hxx"


using namespace ::dp_misc;
using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::ucb;
using ::rtl::OUString;

namespace dp_registry {
namespace backend {

//______________________________________________________________________________
PackageRegistryBackend::~PackageRegistryBackend()
{
}

//______________________________________________________________________________
void PackageRegistryBackend::disposing( lang::EventObject const & event )
    throw (RuntimeException)
{
    Reference<deployment::XPackage> xPackage(
        event.Source, UNO_QUERY_THROW );
    OUString url( xPackage->getURL() );
    ::osl::MutexGuard guard( getMutex() );
    if ( m_bound.erase( url ) != 1 )
    {
        OSL_ASSERT( false );
    }
}

//______________________________________________________________________________
PackageRegistryBackend::PackageRegistryBackend(
    Sequence<Any> const & args,
    Reference<XComponentContext> const & xContext )
    : t_BackendBase( getMutex() ),
      m_xComponentContext( xContext ),
      m_eContext( CONTEXT_UNKNOWN ),
      m_readOnly( false )
{
    boost::optional<OUString> cachePath;
    boost::optional<bool> readOnly;
    comphelper::unwrapArgs( args, m_context, cachePath, readOnly );
    if (cachePath)
        m_cachePath = *cachePath;
    if (readOnly)
        m_readOnly = *readOnly;
    
    if (m_context.equalsAsciiL( RTL_CONSTASCII_STRINGPARAM("user") ))
        m_eContext = CONTEXT_USER;
    else if (m_context.equalsAsciiL( RTL_CONSTASCII_STRINGPARAM("shared") ))
        m_eContext = CONTEXT_SHARED;
    else if (m_context.equalsAsciiL( RTL_CONSTASCII_STRINGPARAM("bundled") ))
        m_eContext = CONTEXT_BUNDLED;
    else if (m_context.equalsAsciiL( RTL_CONSTASCII_STRINGPARAM("tmp") ))
        m_eContext = CONTEXT_TMP;
    else if (m_context.equalsAsciiL( RTL_CONSTASCII_STRINGPARAM("bundled_prereg") ))
        m_eContext = CONTEXT_BUNDLED_PREREG;
    else if (m_context.matchIgnoreAsciiCaseAsciiL(
                 RTL_CONSTASCII_STRINGPARAM("vnd.sun.star.tdoc:/") ))
        m_eContext = CONTEXT_DOCUMENT;
    else
        m_eContext = CONTEXT_UNKNOWN;
}

//______________________________________________________________________________
void PackageRegistryBackend::check()
{
    ::osl::MutexGuard guard( getMutex() );
    if (rBHelper.bInDispose || rBHelper.bDisposed) {
        throw lang::DisposedException(
            OUSTR("PackageRegistryBackend instance has already been disposed!"),
            static_cast<OWeakObject *>(this) );
    }
}

//______________________________________________________________________________
void PackageRegistryBackend::disposing()
{
    try {
        for ( t_string2ref::const_iterator i = m_bound.begin(); i != m_bound.end(); i++)
            i->second->removeEventListener(this);
        m_bound.clear();
        m_xComponentContext.clear();
        WeakComponentImplHelperBase::disposing();    
    }
    catch (RuntimeException &) {
        throw;
    }
    catch (Exception &) {
        Any exc( ::cppu::getCaughtException() );
        throw lang::WrappedTargetRuntimeException(
            OUSTR("caught unexpected exception while disposing!"),
            static_cast<OWeakObject *>(this), exc );
    }
}

// XPackageRegistry
//______________________________________________________________________________
Reference<deployment::XPackage> PackageRegistryBackend::bindPackage(
    OUString const & url, OUString const & mediaType, sal_Bool  bRemoved,
    OUString const & identifier, Reference<XCommandEnvironment> const & xCmdEnv )
    throw (deployment::DeploymentException,
           deployment::InvalidRemovedParameterException,
           ucb::CommandFailedException,
           lang::IllegalArgumentException, RuntimeException)
{
    ::osl::ResettableMutexGuard guard( getMutex() );
    check();

    t_string2ref::const_iterator const iFind( m_bound.find( url ) );
    if (iFind != m_bound.end())     
    {
        Reference<deployment::XPackage> xPackage( iFind->second );
        if (xPackage.is())        
        {
            if (mediaType.getLength() &&
                mediaType != xPackage->getPackageType()->getMediaType())
                throw lang::IllegalArgumentException
                    (OUSTR("XPackageRegistry::bindPackage: media type does not match"),
                     static_cast<OWeakObject*>(this), 1);
            if (xPackage->isRemoved() != bRemoved)
                throw deployment::InvalidRemovedParameterException(
                    OUSTR("XPackageRegistry::bindPackage: bRemoved parameter does not match"),
                    static_cast<OWeakObject*>(this), xPackage->isRemoved(), xPackage);
            return xPackage;  
        }
    }

    guard.clear();
    
    Reference<deployment::XPackage> xNewPackage;
    try {
        xNewPackage = bindPackage_( url, mediaType, bRemoved, 
            identifier, xCmdEnv );
    }
    catch (RuntimeException &) {
        throw;
    }
    catch (lang::IllegalArgumentException &) {
        throw;
    }
    catch (CommandFailedException &) {
        throw;
    }
    catch (deployment::DeploymentException &) {
        throw;
    }
    catch (Exception &) {
        Any exc( ::cppu::getCaughtException() );
        throw deployment::DeploymentException(
            OUSTR("Error binding package: ") + url,
            static_cast<OWeakObject *>(this), exc );
    }
    
    guard.reset();

    ::std::pair< t_string2ref::iterator, bool > insertion(
        m_bound.insert( t_string2ref::value_type( url, xNewPackage ) ) );
    if (insertion.second)
    { // first insertion
        OSL_ASSERT( Reference<XInterface>(insertion.first->second)
                    == xNewPackage );
    }
    else
    { // found existing entry
        Reference<deployment::XPackage> xPackage( insertion.first->second );
        if (xPackage.is())
            return xPackage;
        insertion.first->second = xNewPackage;
    }

    guard.clear();
    xNewPackage->addEventListener( this ); // listen for disposing events
    return xNewPackage;
}

OUString PackageRegistryBackend::createFolder(
    OUString const & relUrl,
    Reference<ucb::XCommandEnvironment> const & xCmdEnv)
{
    const OUString sDataFolder = makeURL(getCachePath(), relUrl);
    //make sure the folder exist
    ucbhelper::Content dataContent;
    ::dp_misc::create_folder(&dataContent, sDataFolder, xCmdEnv);
    
    const OUString sDataFolderURL = dp_misc::expandUnoRcUrl(sDataFolder);
    const String baseDir(sDataFolder);
    const ::utl::TempFile aTemp(&baseDir, sal_True);
    const OUString url = aTemp.GetURL();
    return sDataFolder + url.copy(url.lastIndexOf('/'));
}

//folderURL can have the extension .tmp or .tmp_
//Before OOo 3.4 the created a tmp file with osl_createTempFile and
//then created a Folder with a same name and a trailing '_'
//If the folderURL has no '_' then there is no corresponding tmp file.
void PackageRegistryBackend::deleteTempFolder(
    OUString const & folderUrl)
{
    if (folderUrl.getLength())
    {
        erase_path( folderUrl, Reference<XCommandEnvironment>(),
                    false /* no throw: ignore errors */ );

        if (folderUrl[folderUrl.getLength() - 1] == '_')
        {
            const OUString  tempFile = folderUrl.copy(0, folderUrl.getLength() - 1);
            erase_path( tempFile, Reference<XCommandEnvironment>(),
                        false /* no throw: ignore errors */ );
        }
    }
}

//usedFolders can contain folder names which have the extension .tmp or .tmp_
//Before OOo 3.4 we created a tmp file with osl_createTempFile and
//then created a Folder with a same name and a trailing '_'
//If the folderURL has no '_' then there is no corresponding tmp file.
void PackageRegistryBackend::deleteUnusedFolders(
    OUString const & relUrl,
    ::std::list< OUString> const & usedFolders)
{
    try
    {
        const OUString sDataFolder = makeURL(getCachePath(), relUrl);
        ::ucbhelper::Content tempFolder( 
            sDataFolder, Reference<ucb::XCommandEnvironment>());
        Reference<sdbc::XResultSet> xResultSet(
            tempFolder.createCursor(
                Sequence<OUString>( &StrTitle::get(), 1 ),
                ::ucbhelper::INCLUDE_FOLDERS_ONLY ) );
        // get all temp directories:
        ::std::vector<OUString> tempEntries;

        char tmp[] = ".tmp";

        //Check for ".tmp_" can be removed after OOo 4.0
        char tmp_[] = ".tmp_";
        while (xResultSet->next())
        {
            OUString title(
                Reference<sdbc::XRow>(
                    xResultSet, UNO_QUERY_THROW )->getString(
                        1 /* Title */ ) );

            if (title.endsWithAsciiL(tmp, sizeof(tmp) - 1)
                || title.endsWithAsciiL(tmp_, sizeof(tmp_) - 1))
                tempEntries.push_back(
                    makeURLAppendSysPathSegment(sDataFolder, title));
        }

        for ( ::std::size_t pos = 0; pos < tempEntries.size(); ++pos )
        {
            if (::std::find( usedFolders.begin(), usedFolders.end(), tempEntries[pos] ) ==
                usedFolders.end())
            {
                deleteTempFolder(tempEntries[pos]);
            }
        }
    }
    catch (ucb::InteractiveAugmentedIOException& e)
    {
        //In case the folder containing all the data folder does not
        //exist yet, we ignore the exception
        if (e.Code != ucb::IOErrorCode_NOT_EXISTING)
            throw e;
    }

}

//##############################################################################

//______________________________________________________________________________
Package::~Package()
{
}

//______________________________________________________________________________
Package::Package( ::rtl::Reference<PackageRegistryBackend> const & myBackend,
                  OUString const & url,
                  OUString const & rName,
                  OUString const & displayName,
                  Reference<deployment::XPackageTypeInfo> const & xPackageType,
                  bool bRemoved,
                  OUString const & identifier)
    : t_PackageBase( getMutex() ),
      m_myBackend( myBackend ),
      m_url( url ),
      m_name( rName ),
      m_displayName( displayName ),
      m_xPackageType( xPackageType ),
      m_bRemoved(bRemoved),
      m_identifier(identifier)
{
    if (m_bRemoved)
    {
        //We use the last segment of the URL
        OSL_ASSERT(m_name.getLength() == 0);
        OUString name = m_url;
        rtl::Bootstrap::expandMacros(name);
        sal_Int32 index = name.lastIndexOf('/');
        if (index != -1 && index < name.getLength())
            m_name = name.copy(index + 1);
    }
}

//______________________________________________________________________________
void Package::disposing()
{
    m_myBackend.clear();
    WeakComponentImplHelperBase::disposing();
}

//______________________________________________________________________________
void Package::check() const
{
    ::osl::MutexGuard guard( getMutex() );
    if (rBHelper.bInDispose || rBHelper.bDisposed) {
        throw lang::DisposedException(
            OUSTR("Package instance has already been disposed!"),
            static_cast<OWeakObject *>(const_cast<Package *>(this)));
    }
}

// XComponent
//______________________________________________________________________________
void Package::dispose() throw (RuntimeException)
{
    //Do not call check here. We must not throw an exception here if the object 
    //is being disposed or is already disposed. See com.sun.star.lang.XComponent
    WeakComponentImplHelperBase::dispose();
}

//______________________________________________________________________________
void Package::addEventListener(
    Reference<lang::XEventListener> const & xListener ) throw (RuntimeException)
{
    //Do not call check here. We must not throw an exception here if the object 
    //is being disposed or is already disposed. See com.sun.star.lang.XComponent
    WeakComponentImplHelperBase::addEventListener( xListener );
}

//______________________________________________________________________________
void Package::removeEventListener(
    Reference<lang::XEventListener> const & xListener ) throw (RuntimeException)
{
    //Do not call check here. We must not throw an exception here if the object 
    //is being disposed or is already disposed. See com.sun.star.lang.XComponent
    WeakComponentImplHelperBase::removeEventListener( xListener );
}

// XModifyBroadcaster
//______________________________________________________________________________
void Package::addModifyListener(
    Reference<util::XModifyListener> const & xListener )
    throw (RuntimeException)
{
    check();
    rBHelper.addListener( ::getCppuType( &xListener ), xListener );
}

//______________________________________________________________________________
void Package::removeModifyListener(
    Reference<util::XModifyListener> const & xListener )
    throw (RuntimeException)
{
    check();
    rBHelper.removeListener( ::getCppuType( &xListener ), xListener );
}

//______________________________________________________________________________
void Package::checkAborted(
    ::rtl::Reference<AbortChannel> const & abortChannel )
{
    if (abortChannel.is() && abortChannel->isAborted()) {
        throw CommandAbortedException(
            OUSTR("abort!"), static_cast<OWeakObject *>(this) );
    }
}

// XPackage
//______________________________________________________________________________
Reference<task::XAbortChannel> Package::createAbortChannel()
    throw (RuntimeException)
{
    check();
    return new AbortChannel;
}

//______________________________________________________________________________
sal_Bool Package::isBundle() throw (RuntimeException)
{
    return false; // default
}

//______________________________________________________________________________
::sal_Int32 Package::checkPrerequisites( 
		const css::uno::Reference< css::task::XAbortChannel >&, 
		const css::uno::Reference< css::ucb::XCommandEnvironment >&,
        sal_Bool) 
		throw (css::deployment::DeploymentException,
               css::deployment::ExtensionRemovedException,
               css::ucb::CommandFailedException, 
               css::ucb::CommandAbortedException, 
               css::uno::RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();
	return 0;
}

//______________________________________________________________________________
::sal_Bool Package::checkDependencies( 
		const css::uno::Reference< css::ucb::XCommandEnvironment >& ) 
		throw (css::deployment::DeploymentException,
               css::deployment::ExtensionRemovedException,
               css::ucb::CommandFailedException, 
               css::uno::RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();
	return true;
}


//______________________________________________________________________________
Sequence< Reference<deployment::XPackage> > Package::getBundle(
    Reference<task::XAbortChannel> const &,
    Reference<XCommandEnvironment> const & )
    throw (deployment::DeploymentException,
           CommandFailedException, CommandAbortedException,
           lang::IllegalArgumentException, RuntimeException)
{
    return Sequence< Reference<deployment::XPackage> >();
}

//______________________________________________________________________________
OUString Package::getName() throw (RuntimeException)
{
    return m_name;
}

beans::Optional<OUString> Package::getIdentifier() throw (RuntimeException)
{
    if (m_bRemoved)
        return beans::Optional<OUString>(true, m_identifier);
    
    return beans::Optional<OUString>();
}

//______________________________________________________________________________
OUString Package::getVersion() throw (
    deployment::ExtensionRemovedException,
    RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();
    return OUString();
}

//______________________________________________________________________________
OUString Package::getURL() throw (RuntimeException)
{
    return m_url;
}

//______________________________________________________________________________
OUString Package::getDisplayName() throw (
    deployment::ExtensionRemovedException, RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();
    return m_displayName;
}

//______________________________________________________________________________
OUString Package::getDescription() throw (
    deployment::ExtensionRemovedException,RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();
    return OUString();
}

//______________________________________________________________________________
OUString Package::getLicenseText() throw (
    deployment::ExtensionRemovedException,RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();
    return OUString();
}

//______________________________________________________________________________
Sequence<OUString> Package::getUpdateInformationURLs() throw (
    deployment::ExtensionRemovedException, RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();    
    return Sequence<OUString>();
}

//______________________________________________________________________________
css::beans::StringPair Package::getPublisherInfo() throw (
    deployment::ExtensionRemovedException, RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();    
    css::beans::StringPair aEmptyPair;
    return aEmptyPair;
}

//______________________________________________________________________________
uno::Reference< css::graphic::XGraphic > Package::getIcon( sal_Bool /*bHighContrast*/ )
    throw (deployment::ExtensionRemovedException, RuntimeException )
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();    

    uno::Reference< css::graphic::XGraphic > aEmpty;
    return aEmpty;
}

//______________________________________________________________________________
Reference<deployment::XPackageTypeInfo> Package::getPackageType()
    throw (RuntimeException)
{
    return m_xPackageType;
}

//______________________________________________________________________________
void Package::exportTo(
    OUString const & destFolderURL, OUString const & newTitle,
    sal_Int32 nameClashAction, Reference<XCommandEnvironment> const & xCmdEnv )
    throw (deployment::ExtensionRemovedException,
           CommandFailedException, CommandAbortedException, RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();    

    ::ucbhelper::Content destFolder( destFolderURL, xCmdEnv );
    ::ucbhelper::Content sourceContent( getURL(), xCmdEnv );
    if (! destFolder.transferContent(
            sourceContent, ::ucbhelper::InsertOperation_COPY,
            newTitle, nameClashAction ))
        throw RuntimeException( OUSTR("UCB transferContent() failed!"), 0 );
}

//______________________________________________________________________________
void Package::fireModified()
{
    ::cppu::OInterfaceContainerHelper * container = rBHelper.getContainer(
        ::getCppuType( static_cast<Reference<
                       util::XModifyListener> const *>(0) ) );
    if (container != 0) {
        Sequence< Reference<XInterface> > elements(
            container->getElements() );
        lang::EventObject evt( static_cast<OWeakObject *>(this) );
        for ( sal_Int32 pos = 0; pos < elements.getLength(); ++pos )
        {
            Reference<util::XModifyListener> xListener(
                elements[ pos ], UNO_QUERY );
            if (xListener.is())
                xListener->modified( evt );
        }
    }
}

// XPackage
//______________________________________________________________________________
beans::Optional< beans::Ambiguous<sal_Bool> > Package::isRegistered(
    Reference<task::XAbortChannel> const & xAbortChannel,
    Reference<XCommandEnvironment> const & xCmdEnv )
    throw (deployment::DeploymentException,
           CommandFailedException, CommandAbortedException, RuntimeException)
{
    try {
        ::osl::ResettableMutexGuard guard( getMutex() );
        return isRegistered_( guard,
                              AbortChannel::get(xAbortChannel),
                              xCmdEnv );
    }
    catch (RuntimeException &) {
        throw;
    }
    catch (CommandFailedException &) {
        throw;
    }
    catch (CommandAbortedException &) {
        throw;
    }
    catch (deployment::DeploymentException &) {
        throw;
    }
    catch (Exception &) {
        Any exc( ::cppu::getCaughtException() );
        throw deployment::DeploymentException(
            OUSTR("unexpected exception occurred!"),
            static_cast<OWeakObject *>(this), exc );
    }
}

//______________________________________________________________________________
void Package::processPackage_impl(
    bool doRegisterPackage,
    bool startup,
    Reference<task::XAbortChannel> const & xAbortChannel,
    Reference<XCommandEnvironment> const & xCmdEnv )
{
    check();
    bool action = false;
    
    try {
        try {
            ::osl::ResettableMutexGuard guard( getMutex() );
            beans::Optional< beans::Ambiguous<sal_Bool> > option(
                isRegistered_( guard, AbortChannel::get(xAbortChannel),
                               xCmdEnv ) );
            action = (option.IsPresent &&
                      (option.Value.IsAmbiguous ||
                       (doRegisterPackage ? !option.Value.Value
                                        : option.Value.Value)));
            if (action) {

                OUString displayName = isRemoved() ? getName() : getDisplayName();
                ProgressLevel progress(
                    xCmdEnv,
                    (doRegisterPackage
                     ? PackageRegistryBackend::StrRegisteringPackage::get()
                     : PackageRegistryBackend::StrRevokingPackage::get())
                    + displayName );
                processPackage_( guard,
                                 doRegisterPackage,
                                 startup,
                                 AbortChannel::get(xAbortChannel),
                                 xCmdEnv );
            }
        }
        catch (RuntimeException &) {
            OSL_ENSURE( 0, "### unexpected RuntimeException!" );
            throw;
        }
        catch (CommandFailedException &) {
            throw;
        }
        catch (CommandAbortedException &) {
            throw;
        }
        catch (deployment::DeploymentException &) {
            throw;
        }
        catch (Exception &) {
            Any exc( ::cppu::getCaughtException() );
            throw deployment::DeploymentException(
                (doRegisterPackage
                 ? getResourceString(RID_STR_ERROR_WHILE_REGISTERING)
                 : getResourceString(RID_STR_ERROR_WHILE_REVOKING))
                + getDisplayName(), static_cast<OWeakObject *>(this), exc );
        }
    }
    catch (...) {
        if (action)
            fireModified();
        throw;
    }
    if (action)
        fireModified();
}

//______________________________________________________________________________
void Package::registerPackage(
    sal_Bool startup,                              
    Reference<task::XAbortChannel> const & xAbortChannel,
    Reference<XCommandEnvironment> const & xCmdEnv )
    throw (deployment::DeploymentException,
           deployment::ExtensionRemovedException,
           CommandFailedException, CommandAbortedException,
           lang::IllegalArgumentException, RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();
    processPackage_impl( true /* register */, startup, xAbortChannel, xCmdEnv );
}

//______________________________________________________________________________
void Package::revokePackage(
    Reference<task::XAbortChannel> const & xAbortChannel,
    Reference<XCommandEnvironment> const & xCmdEnv )
    throw (deployment::DeploymentException,
           CommandFailedException, CommandAbortedException,
           lang::IllegalArgumentException, RuntimeException)
{
    processPackage_impl( false /* revoke */, false, xAbortChannel, xCmdEnv );

}

PackageRegistryBackend * Package::getMyBackend() const
{
    PackageRegistryBackend * pBackend = m_myBackend.get();
    if (NULL == pBackend)
    {    
        //May throw a DisposedException
        check();
        //We should never get here...
        throw RuntimeException(
            OUSTR("Failed to get the BackendImpl"), 
            static_cast<OWeakObject*>(const_cast<Package *>(this)));
    }
    return pBackend;
}
OUString Package::getRepositoryName()
    throw (RuntimeException)
{
    PackageRegistryBackend * backEnd = getMyBackend();
    return backEnd->getContext();
}

beans::Optional< OUString > Package::getRegistrationDataURL()
        throw (deployment::ExtensionRemovedException,
               css::uno::RuntimeException)
{
    if (m_bRemoved)
        throw deployment::ExtensionRemovedException();
    return beans::Optional<OUString>();    
}

sal_Bool Package::isRemoved()
    throw (RuntimeException)
{
    return m_bRemoved;
}

//##############################################################################

//______________________________________________________________________________
Package::TypeInfo::~TypeInfo()
{
}

// XPackageTypeInfo
//______________________________________________________________________________
OUString Package::TypeInfo::getMediaType() throw (RuntimeException)
{
    return m_mediaType;
}

//______________________________________________________________________________
OUString Package::TypeInfo::getDescription()
    throw (deployment::ExtensionRemovedException, RuntimeException)
{
    return getShortDescription();
}

//______________________________________________________________________________
OUString Package::TypeInfo::getShortDescription()
    throw (deployment::ExtensionRemovedException, RuntimeException)
{
    return m_shortDescr;
}

//______________________________________________________________________________
OUString Package::TypeInfo::getFileFilter() throw (RuntimeException)
{
    return m_fileFilter;
}

//______________________________________________________________________________
Any Package::TypeInfo::getIcon( sal_Bool highContrast, sal_Bool smallIcon )
    throw (RuntimeException)
{
    if (! smallIcon)
        return Any();
    const sal_uInt16 nIconId = (highContrast ? m_smallIcon_HC : m_smallIcon);
    return Any( &nIconId, getCppuType( static_cast<sal_uInt16 const *>(0) ) );
}

}
}

