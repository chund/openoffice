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



#include "oox/helper/propertymap.hxx"

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XPropertySetInfo.hpp>
#include <cppuhelper/implbase2.hxx>
#include <osl/mutex.hxx>
#include "oox/token/propertynames.hxx"

namespace oox {

// ============================================================================

using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::uno;

using ::rtl::OString;
using ::rtl::OUString;

// ============================================================================

namespace {

typedef ::cppu::WeakImplHelper2< XPropertySet, XPropertySetInfo > GenericPropertySetBase;

/** This class implements a generic XPropertySet.

    Properties of all names and types can be set and later retrieved.
    TODO: move this to comphelper or better find an existing implementation
 */
class GenericPropertySet : public GenericPropertySetBase, private ::osl::Mutex
{
public:
    explicit            GenericPropertySet();
    explicit            GenericPropertySet( const PropertyMap& rPropMap );

    // XPropertySet
    virtual Reference< XPropertySetInfo > SAL_CALL getPropertySetInfo() throw (RuntimeException);
    virtual void SAL_CALL setPropertyValue( const OUString& aPropertyName, const Any& aValue ) throw (UnknownPropertyException, PropertyVetoException, IllegalArgumentException, WrappedTargetException, RuntimeException);
    virtual Any SAL_CALL getPropertyValue( const OUString& PropertyName ) throw (UnknownPropertyException, WrappedTargetException, RuntimeException);
    virtual void SAL_CALL addPropertyChangeListener( const OUString& aPropertyName, const Reference< XPropertyChangeListener >& xListener ) throw (UnknownPropertyException, WrappedTargetException, RuntimeException);
    virtual void SAL_CALL removePropertyChangeListener( const OUString& aPropertyName, const Reference< XPropertyChangeListener >& aListener ) throw (UnknownPropertyException, WrappedTargetException, RuntimeException);
    virtual void SAL_CALL addVetoableChangeListener( const OUString& PropertyName, const Reference< XVetoableChangeListener >& aListener ) throw (UnknownPropertyException, WrappedTargetException, RuntimeException);
    virtual void SAL_CALL removeVetoableChangeListener( const OUString& PropertyName, const Reference< XVetoableChangeListener >& aListener ) throw (UnknownPropertyException, WrappedTargetException, RuntimeException);

    // XPropertySetInfo
    virtual Sequence< Property > SAL_CALL getProperties() throw (RuntimeException);
    virtual Property SAL_CALL getPropertyByName( const OUString& aName ) throw (UnknownPropertyException, RuntimeException);
    virtual sal_Bool SAL_CALL hasPropertyByName( const OUString& Name ) throw (RuntimeException);

private:
    typedef ::std::map< OUString, Any > PropertyNameMap;
    PropertyNameMap     maPropMap;
};

// ----------------------------------------------------------------------------

GenericPropertySet::GenericPropertySet()
{
}

GenericPropertySet::GenericPropertySet( const PropertyMap& rPropMap )
{
    const PropertyNameVector& rPropNames = StaticPropertyNameVector::get();
    for( PropertyMap::const_iterator aIt = rPropMap.begin(), aEnd = rPropMap.end(); aIt != aEnd; ++aIt )
        maPropMap[ rPropNames[ aIt->first ] ] = aIt->second;
}

Reference< XPropertySetInfo > SAL_CALL GenericPropertySet::getPropertySetInfo() throw (RuntimeException)
{
    return this;
}

void SAL_CALL GenericPropertySet::setPropertyValue( const OUString& rPropertyName, const Any& rValue ) throw (UnknownPropertyException, PropertyVetoException, IllegalArgumentException, WrappedTargetException, RuntimeException)
{
    ::osl::MutexGuard aGuard( *this );
    maPropMap[ rPropertyName ] = rValue;
}

Any SAL_CALL GenericPropertySet::getPropertyValue( const OUString& rPropertyName ) throw (UnknownPropertyException, WrappedTargetException, RuntimeException)
{
    PropertyNameMap::iterator aIt = maPropMap.find( rPropertyName );
    if( aIt == maPropMap.end() )
        throw UnknownPropertyException();
    return aIt->second;
}

// listeners are not supported by this implementation
void SAL_CALL GenericPropertySet::addPropertyChangeListener( const OUString& , const Reference< XPropertyChangeListener >& ) throw (UnknownPropertyException, WrappedTargetException, RuntimeException) {}
void SAL_CALL GenericPropertySet::removePropertyChangeListener( const OUString& , const Reference< XPropertyChangeListener >&  ) throw (UnknownPropertyException, WrappedTargetException, RuntimeException) {}
void SAL_CALL GenericPropertySet::addVetoableChangeListener( const OUString& , const Reference< XVetoableChangeListener >&  ) throw (UnknownPropertyException, WrappedTargetException, RuntimeException) {}
void SAL_CALL GenericPropertySet::removeVetoableChangeListener( const OUString& , const Reference< XVetoableChangeListener >&  ) throw (UnknownPropertyException, WrappedTargetException, RuntimeException) {}

// XPropertySetInfo
Sequence< Property > SAL_CALL GenericPropertySet::getProperties() throw (RuntimeException)
{
    Sequence< Property > aSeq( static_cast< sal_Int32 >( maPropMap.size() ) );
    Property* pProperty = aSeq.getArray();
    for( PropertyNameMap::iterator aIt = maPropMap.begin(), aEnd = maPropMap.end(); aIt != aEnd; ++aIt, ++pProperty )
    {
        pProperty->Name = aIt->first;
        pProperty->Handle = 0;
        pProperty->Type = aIt->second.getValueType();
        pProperty->Attributes = 0;
    }
    return aSeq;
}

Property SAL_CALL GenericPropertySet::getPropertyByName( const OUString& rPropertyName ) throw (UnknownPropertyException, RuntimeException)
{
    PropertyNameMap::iterator aIt = maPropMap.find( rPropertyName );
    if( aIt == maPropMap.end() )
        throw UnknownPropertyException();
    Property aProperty;
    aProperty.Name = aIt->first;
    aProperty.Handle = 0;
    aProperty.Type = aIt->second.getValueType();
    aProperty.Attributes = 0;
    return aProperty;
}

sal_Bool SAL_CALL GenericPropertySet::hasPropertyByName( const OUString& rPropertyName ) throw (RuntimeException)
{
    return maPropMap.find( rPropertyName ) != maPropMap.end();
}

} // namespace

// ============================================================================

PropertyMap::PropertyMap() :
    mpPropNames( &StaticPropertyNameVector::get() ) // pointer instead reference to get compiler generated copy c'tor and operator=
{
}

/*static*/ const OUString& PropertyMap::getPropertyName( sal_Int32 nPropId )
{
    OSL_ENSURE( (0 <= nPropId) && (nPropId < PROP_COUNT), "PropertyMap::getPropertyName - invalid property identifier" );
    return StaticPropertyNameVector::get()[ nPropId ];
}

const Any* PropertyMap::getProperty( sal_Int32 nPropId ) const
{
    const_iterator aIt = find( nPropId );
    return (aIt == end()) ? 0 : &aIt->second;
}

Sequence< PropertyValue > PropertyMap::makePropertyValueSequence() const
{
    Sequence< PropertyValue > aSeq( static_cast< sal_Int32 >( size() ) );
    if( !empty() )
    {
        PropertyValue* pValues = aSeq.getArray();
        for( const_iterator aIt = begin(), aEnd = end(); aIt != aEnd; ++aIt, ++pValues )
        {
            OSL_ENSURE( (0 <= aIt->first) && (aIt->first < PROP_COUNT), "PropertyMap::makePropertyValueSequence - invalid property identifier" );
            pValues->Name = (*mpPropNames)[ aIt->first ];
            pValues->Value = aIt->second;
            pValues->State = ::com::sun::star::beans::PropertyState_DIRECT_VALUE;
        }
    }
    return aSeq;
}

void PropertyMap::fillSequences( Sequence< OUString >& rNames, Sequence< Any >& rValues ) const
{
    rNames.realloc( static_cast< sal_Int32 >( size() ) );
    rValues.realloc( static_cast< sal_Int32 >( size() ) );
    if( !empty() )
    {
        OUString* pNames = rNames.getArray();
        Any* pValues = rValues.getArray();
        for( const_iterator aIt = begin(), aEnd = end(); aIt != aEnd; ++aIt, ++pNames, ++pValues )
        {
            OSL_ENSURE( (0 <= aIt->first) && (aIt->first < PROP_COUNT), "PropertyMap::fillSequences - invalid property identifier" );
            *pNames = (*mpPropNames)[ aIt->first ];
            *pValues = aIt->second;
        }
    }
}

Reference< XPropertySet > PropertyMap::makePropertySet() const
{
    return new GenericPropertySet( *this );
}

// ============================================================================

} // namespace oox
