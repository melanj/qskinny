/******************************************************************************
 * QSkinny - Copyright (C) The authors
 *           SPDX-License-Identifier: BSD-3-Clause
 *****************************************************************************/

#include "QskMetaInvokable.h"
#include "QskMetaFunction.h"
#include "QskInternalMacros.h"

#include <qmetaobject.h>
#include <qobject.h>
#include <qcoreapplication.h>
#include <qthread.h>

#if QT_CONFIG(thread)
#include <qsemaphore.h>
#endif

QSK_QT_PRIVATE_BEGIN
#include <private/qobject_p.h>
QSK_QT_PRIVATE_END

static void qskRegisterMetaInvokable()
{
    qRegisterMetaType< QskMetaInvokable >();
}

Q_CONSTRUCTOR_FUNCTION( qskRegisterMetaInvokable )

namespace
{
    using CallFunction = QObjectPrivate::StaticMetaCallFunction;

    class Function : public QskMetaFunction
    {
      public:
        inline Function( void* functionCall )
            : QskMetaFunction( static_cast< FunctionCall* >( functionCall ) )
        {
        }

        static inline void ref( void* functionCall )
        {
            if ( functionCall )
                static_cast< FunctionCall* >( functionCall )->ref();
        }

        static inline void deref( void* functionCall )
        {
            if ( functionCall )
                static_cast< FunctionCall* >( functionCall )->destroyIfLastRef();
        }
    };

    class MetaCallEvent final : public QMetaCallEvent
    {
      public:
        MetaCallEvent( QMetaObject::Call call, const QMetaObject* metaObject,
                ushort offset, ushort index, void* args[], QSemaphore* semaphore )
            : QMetaCallEvent( offset, index,
                metaObject->d.static_metacall, nullptr, -1, args, semaphore )
            , m_call( call )
            , m_callFunction( metaObject->d.static_metacall )
            , m_index( index )
        {
        }

        MetaCallEvent( QMetaObject::Call call, const QMetaObject* metaObject,
                ushort offset, ushort index, int argc )
            : QMetaCallEvent( offset, index,
                metaObject->d.static_metacall, nullptr, -1, argc )
            , m_call( call )
            , m_callFunction( metaObject->d.static_metacall )
            , m_index( index )
        {
        }

        void placeMetaCall( QObject* object ) override
        {
            m_callFunction( object, m_call, m_index, args() );
        }

      private:
        const QMetaObject::Call m_call;

        // as those members from QMetaCallEvent are not accessible
        CallFunction m_callFunction;
        const ushort m_index;
    };
}

QMetaMethod qskMetaMethod( const QObject* object, const char* methodName )
{
    return object ? qskMetaMethod( object->metaObject(), methodName ) : QMetaMethod();
}

QMetaMethod qskMetaMethod( const QMetaObject* metaObject, const char* methodName )
{
    if ( metaObject == nullptr || methodName == nullptr )
        return QMetaMethod();

    constexpr char signalIndicator = '0' + QSIGNAL_CODE;
    constexpr char slotIndicator = '0' + QSLOT_CODE;

    int index = -1;

    if ( methodName[ 0 ] == signalIndicator )
    {
        auto signature = QMetaObject::normalizedSignature( methodName + 1 );
        index = metaObject->indexOfSignal( signature );
    }
    else if ( methodName[ 0 ] == slotIndicator )
    {
        auto signature = QMetaObject::normalizedSignature( methodName + 1 );
        index = metaObject->indexOfSlot( signature );
    }
    else
    {
        auto signature = QMetaObject::normalizedSignature( methodName );
        index = metaObject->indexOfMethod( signature );
    }

    return ( index >= 0 ) ? metaObject->method( index ) : QMetaMethod();
}

QMetaMethod qskNotifySignal( const QObject* object, const char* propertyName )
{
    return object ? qskNotifySignal( object->metaObject(), propertyName ) : QMetaMethod();
}

QMetaMethod qskNotifySignal( const QMetaObject* metaObject, const char* propertyName )
{
    if ( metaObject == nullptr || propertyName == nullptr )
        return QMetaMethod();

    const int propertyIndex = metaObject->indexOfProperty( propertyName );
    if ( propertyIndex )
    {
        const auto property = metaObject->property( propertyIndex );
        return property.notifySignal();
    }

    return QMetaMethod();
}

static void qskInvokeMetaCall(
    QObject* object, const QMetaObject* metaObject,
    QMetaObject::Call call, ushort offset, ushort index, void* args[],
    Qt::ConnectionType connectionType )
{
    QPointer< QObject > receiver( object );

    int invokeType = connectionType & 0x3;

    if ( invokeType == Qt::AutoConnection )
    {
        invokeType = ( object && object->thread() != QThread::currentThread() )
            ? Qt::QueuedConnection : Qt::DirectConnection;
    }

    switch ( invokeType )
    {
        case Qt::DirectConnection:
        {
            if ( receiver.isNull() )
            {
#if 1
                // do we really always need an object, what about Q_GADGET ???
                return;
#endif
            }

            /*
                QMetaObject::metacall seems to be made for situations we don't have.
                Need to dive deeper into the Qt code to be 100% sure TODO ...
             */

            metaObject->d.static_metacall( receiver, call, index, args );
            break;
        }
        case Qt::BlockingQueuedConnection:
        {
            if ( receiver.isNull() ||
                ( receiver->thread() == QThread::currentThread() ) )
            {
                // We would end up in a deadlock, better do nothing
                return;
            }

#if QT_CONFIG(thread)
            QSemaphore semaphore;

            auto event = new MetaCallEvent( call, metaObject,
                offset, index, args, &semaphore );

#else
            auto event = new MetaCallEvent( call, metaObject,
                offset, index, args, nullptr );
#endif

            QCoreApplication::postEvent( receiver, event );

#if QT_CONFIG(thread)
            semaphore.acquire();
#endif

            break;
        }
        case Qt::QueuedConnection:
        {
            if ( receiver == nullptr )
                return;

            MetaCallEvent* event = nullptr;

            if ( call == QMetaObject::InvokeMetaMethod )
            {
#if 1
                // should be doable without QMetaMethod. TODO ...
                const auto method = metaObject->method( offset + index );
#endif
                const int argc = method.parameterCount() + 1;

                event = new MetaCallEvent( call, metaObject, offset, index, argc );

                /*
                    The first one is the return type, one that is always
                    invalid for Queued Connections.
                 */

                auto types = event->types();
                auto arguments = event->args();

#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
                types[0] = QMetaType();
                arguments[ 0 ] = nullptr;
#else
                types[0] = 0;
                arguments[ 0 ] = nullptr;
#endif
                for ( int i = 1; i < argc; i++ )
                {
                    if ( args[ i ] == nullptr )
                    {
                        Q_ASSERT( args[ i ] != nullptr );
                        receiver = nullptr;
                        break;
                    }

                    const auto type = method.parameterType( i - 1 );
                    const auto arg = args[ i ];

#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
                    types[ i ] = QMetaType( type );
                    arguments[ i ] = QMetaType( type ).create( arg );
#else
                    types[ i ] = type;
                    arguments[ i ] = QMetaType::create( type, arg );
#endif
                }
            }
            else
            {
                // should be doable without QMetaMethod. TODO ...
                const auto property = metaObject->property( offset + index );

                event = new MetaCallEvent( call, metaObject, offset, index, 1 );

                const auto type = property.userType();
                const auto arg = args[ 0 ];

#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
                event->types()[0] = QMetaType( type );
                event->args()[ 0 ] = QMetaType( type ).create( arg );
#else
                event->types()[0] = type;
                event->args()[ 0 ] = QMetaType::create( type, arg );
#endif
            }

            if ( receiver )
                QCoreApplication::postEvent( receiver, event );
            else
                delete event;

            break;
        }
    }
}

void qskInvokeMetaPropertyWrite( QObject* context, const QMetaProperty& property,
    void* args[], Qt::ConnectionType connectionType )
{
    qskInvokeMetaPropertyWrite( context, property.enclosingMetaObject(),
        property.propertyIndex(), args, connectionType );
}

void qskInvokeMetaPropertyWrite( QObject* context, const QMetaObject* metaObject,
    int propertyIndex, void* args[], Qt::ConnectionType connectionType )
{
    // check for is writable ???

    if ( metaObject && ( propertyIndex >= 0 ) &&
        ( propertyIndex < metaObject->propertyCount() ) )
    {
        const auto offset = metaObject->propertyOffset();
        const auto index = propertyIndex - offset;

        qskInvokeMetaCall( context, metaObject, QMetaObject::WriteProperty,
            offset, index, args + 1, connectionType );
    }
}

void qskInvokeMetaMethod( QObject* object,
    const QMetaMethod& method, void* args[],
    Qt::ConnectionType connectionType )
{
    qskInvokeMetaMethod( object, method.enclosingMetaObject(),
        method.methodIndex(), args, connectionType );
}

void qskInvokeMetaMethod( QObject* object,
    const QMetaObject* metaObject, int methodIndex, void* args[],
    Qt::ConnectionType connectionType )
{
    if ( metaObject && ( methodIndex >= 0 ) &&
        ( methodIndex < metaObject->methodCount() ) )
    {
        const auto offset = metaObject->methodOffset();
        const auto index = methodIndex - offset;

        qskInvokeMetaCall( object, metaObject, QMetaObject::InvokeMetaMethod,
            offset, index, args, connectionType );
    }
}

QskMetaInvokable::QskMetaInvokable( const QMetaMethod& method )
    : m_metaData{ method.enclosingMetaObject(), method.methodIndex() }
    , m_type( MetaMethod )
{
}

QskMetaInvokable::QskMetaInvokable( const QObject* object, const char* methodName )
    : QskMetaInvokable( qskMetaMethod( object, methodName ) )
{
}

QskMetaInvokable::QskMetaInvokable( const QMetaObject* metaObject, const char* methodName )
    : QskMetaInvokable( qskMetaMethod( metaObject, methodName ) )
{
}

QskMetaInvokable::QskMetaInvokable( const QMetaProperty& property )
    : m_metaData{ property.enclosingMetaObject(), property.propertyIndex() }
    , m_type( MetaProperty )
{
}

QskMetaInvokable::QskMetaInvokable( const QskMetaFunction& function )
    : m_functionData{ function.functionCall() }
    , m_type( MetaFunction )
{
    Function::ref( m_functionData.functionCall );
}

QskMetaInvokable::QskMetaInvokable( const QskMetaInvokable& other )
    : m_type( other.m_type )
{
    switch ( m_type )
    {
        case MetaMethod:
        case MetaProperty:
        {
            m_metaData.metaObject = other.m_metaData.metaObject;
            m_metaData.index = other.m_metaData.index;

            break;
        }
        case MetaFunction:
        {
            m_functionData.functionCall = other.m_functionData.functionCall;
            Function::ref( m_functionData.functionCall );

            break;
        }

        default:
            break;
    }
}

QskMetaInvokable::~QskMetaInvokable()
{
    if ( m_type == MetaFunction )
        Function::deref( m_functionData.functionCall );
}

QskMetaInvokable& QskMetaInvokable::operator=( const QskMetaInvokable& other )
{
    switch ( other.m_type )
    {
        case MetaMethod:
        case MetaProperty:
        {
            if ( m_type == MetaFunction )
                Function::deref( m_functionData.functionCall );

            m_metaData.metaObject = other.m_metaData.metaObject;
            m_metaData.index = other.m_metaData.index;

            break;
        }
        case MetaFunction:
        {
            if ( m_type == MetaFunction )
                Function::deref( m_functionData.functionCall );

            m_functionData.functionCall = other.m_functionData.functionCall;
            Function::ref( m_functionData.functionCall );

            break;
        }

        default:
            if ( m_type == MetaFunction )
                Function::deref( m_functionData.functionCall );
    }

    m_type = other.m_type;

    return *this;
}

bool QskMetaInvokable::operator==( const QskMetaInvokable& other ) const
{
    if ( m_type != other.m_type )
        return false;

    switch ( m_type )
    {
        case MetaMethod:
        case MetaProperty:
        {
            return ( m_metaData.metaObject == other.m_metaData.metaObject ) &&
                   ( m_metaData.index == other.m_metaData.index );
        }
        case MetaFunction:
        {
            return m_functionData.functionCall == other.m_functionData.functionCall;
        }
        default:
        {
            return true;
        }
    }
}

bool QskMetaInvokable::isNull() const
{
    switch ( m_type )
    {
        case MetaMethod:
        case MetaProperty:
        {
            const auto& d = m_metaData;

            if ( d.metaObject == nullptr || d.index < 0 )
                return true;

            const int count = ( m_type == MetaMethod )
                ? d.metaObject->methodCount() : d.metaObject->propertyCount();

            return d.index >= count;
        }
        case MetaFunction:
        {
            return m_functionData.functionCall == nullptr;
        }

        default:
            return true;
    }
}

void QskMetaInvokable::reset()
{
    if ( m_type == MetaFunction )
        Function::deref( m_functionData.functionCall );

    m_type = Invalid;
}

int QskMetaInvokable::parameterCount() const
{
    switch ( m_type )
    {
        case MetaMethod:
        {
            // should be doable without QMetaMethod. TODO ...
            const auto method = QskMetaInvokable::method();
            return method.parameterCount();
        }
        case MetaProperty:
        {
            return 1;
        }
        case MetaFunction:
        {
            return function().parameterCount();
        }
        default:
            break;
    }

    return 0;
}

int QskMetaInvokable::parameterType( int index ) const
{
    switch ( m_type )
    {
        case MetaMethod:
        {
            const auto method = QskMetaInvokable::method();
            return method.parameterType( index );
        }
        case MetaProperty:
        {
            const auto property = QskMetaInvokable::property();
            return property.userType();
        }
        case MetaFunction:
        {
            auto types = function().parameterTypes();
            return types[ index ];
        }
        default:
        {
            return QMetaType::UnknownType;
        }
    }
}

int QskMetaInvokable::returnType() const
{
    switch ( m_type )
    {
        case MetaMethod:
        {
            return method().returnType();
        }
        case MetaFunction:
        {
            return function().returnType();
        }
        case MetaProperty:
        {
            return QMetaType::Void;
        }
        default:
        {
            return QMetaType::Void;
        }
    }
}

QByteArray QskMetaInvokable::name() const
{
    switch ( m_type )
    {
        case MetaMethod:
        {
            return method().name();
        }
        case MetaProperty:
        {
            return property().name();
        }
        case MetaFunction:
        {
            // what to do here ???
            return QByteArray();
        }
        default:
        {
            return QByteArray();
        }
    }
}

QMetaMethod QskMetaInvokable::method() const
{
    if ( m_type == MetaMethod && m_metaData.metaObject )
        return m_metaData.metaObject->method( m_metaData.index );

    return QMetaMethod();
}

QMetaProperty QskMetaInvokable::property() const
{
    if ( m_type == MetaProperty && m_metaData.metaObject )
        return m_metaData.metaObject->property( m_metaData.index );

    return QMetaProperty();
}

QskMetaFunction QskMetaInvokable::function() const
{
    if ( m_type == MetaFunction && m_functionData.functionCall )
    {
        Function function( m_functionData.functionCall );
        return *static_cast< QskMetaFunction* >( &function );
    }

    return QskMetaFunction();
}

void QskMetaInvokable::invoke( QObject* object, void* args[],
    Qt::ConnectionType connectionType )
{
    if ( isNull() )
        return;

    switch ( m_type )
    {
        case MetaMethod:
        {
            qskInvokeMetaMethod( object, m_metaData.metaObject,
                m_metaData.index, args, connectionType );

            break;
        }
        case MetaProperty:
        {
            qskInvokeMetaPropertyWrite(
                object, m_metaData.metaObject, m_metaData.index, args, connectionType );

            break;
        }
        case MetaFunction:
        {
            if ( m_functionData.functionCall )
            {
                Function function( m_functionData.functionCall );
                function.invoke( object, args, connectionType );
            }

            break;
        }

        default:
            break;
    }
}
