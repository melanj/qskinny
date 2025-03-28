/******************************************************************************
 * QSkinny - Copyright (C) The authors
 *           SPDX-License-Identifier: BSD-3-Clause
 *****************************************************************************/

#include "QskQuick.h"
#include "QskControl.h"
#include "QskFunctions.h"
#include "QskLayoutElement.h"
#include "QskPlatform.h"
#include "QskInternalMacros.h"

#include <qquickitem.h>

QSK_QT_PRIVATE_BEGIN
#include <private/qquickitem_p.h>
#include <private/qquickwindow_p.h>
#include <private/qsgrenderer_p.h>
QSK_QT_PRIVATE_END

#include <qpa/qplatforminputcontext.h>
#include <qpa/qplatformintegration.h>

QRhi* qskRenderingHardwareInterface( const QQuickWindow* window )
{
    if ( auto w = const_cast< QQuickWindow* >( window ) )
        return QQuickWindowPrivate::get( w )->rhi;

    return nullptr;
}

bool qskIsOpenGLWindow( const QQuickWindow* window )
{       
    if ( window == nullptr )
        return false;
    
    const auto renderer = window->rendererInterface();
    switch( renderer->graphicsApi() )
    {
        case QSGRendererInterface::OpenGL:
#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
        case QSGRendererInterface::OpenGLRhi:
#endif
            return true;

        default:
            return false;
    }   
}

QRectF qskItemRect( const QQuickItem* item )
{
    auto d = QQuickItemPrivate::get( item );
    return QRectF( 0, 0, d->width, d->height );
}

QRectF qskItemGeometry( const QQuickItem* item )
{
    auto d = QQuickItemPrivate::get( item );
    return QRectF( d->x, d->y, d->width, d->height );
}

void qskSetItemGeometry( QQuickItem* item, const QRectF& rect )
{
    if ( auto control = qskControlCast( item ) )
    {
        control->setGeometry( rect );
    }
    else if ( item )
    {
        item->setPosition( rect.topLeft() );
        item->setSize( rect.size() );
    }
}

bool qskIsItemComplete( const QQuickItem* item )
{
    return QQuickItemPrivate::get( item )->componentComplete;
}

bool qskIsItemInDestructor( const QQuickItem* item )
{
    if ( item == nullptr )
        return false;

    auto d = QQuickItemPrivate::get( item );

#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
    return d->inDestructor;
#else
    /*
        QskItem sets componentComplete to false in its destructor,
        but for other items we will will return the wrong information
     */
    return !d->componentComplete;
#endif
}

bool qskIsAncestorOf( const QQuickItem* item, const QQuickItem* child )
{
    if ( item == nullptr || child == nullptr )
        return false;

    return item->isAncestorOf( child );
}

bool qskIsVisibleToParent( const QQuickItem* item )
{
    if ( item == nullptr )
        return false;

    return QQuickItemPrivate::get( item )->explicitVisible;
}

bool qskIsVisibleTo( const QQuickItem* item, const QQuickItem* ancestor )
{
    if ( item == nullptr )
        return false;

    if ( ancestor == nullptr )
        return item->isVisible(); // like QWidget::isVisibleTo

    for ( auto it = item->parentItem();
        it != ancestor; it = it->parentItem() )
    {
        if ( it == nullptr )
            return false; // ancestor is no parent

        if ( !QQuickItemPrivate::get( it )->explicitVisible )
            return false;
    }

    return true;
}

bool qskIsTabFence( const QQuickItem* item )
{
    if ( item == nullptr )
        return false;

    return QQuickItemPrivate::get( item )->isTabFence;
}

bool qskIsPolishScheduled( const QQuickItem* item )
{
    if ( item )
        return QQuickItemPrivate::get( item )->polishScheduled;

    return false;
}

bool qskIsShortcutScope( const QQuickItem* item )
{
    if ( item == nullptr )
        return false;

    /*
        We might have something like CTRL+W to close a "window".
        But in Qt/Quick a window is not necessarily a QQuickWindow
        like we have f.e QskSubWindow.

        Maybe it's worth to introduce a shortcutScope flag but for
        the moment we simply use the isFocusScope/isTabFence combination,
        that should usually be set for those "windows".
     */

    return item->isFocusScope() && QQuickItemPrivate::get( item )->isTabFence;
}

bool qskIsVisibleToLayout( const QQuickItem* item )
{
    return qskEffectivePlacementPolicy( item ) != QskPlacementPolicy::Ignore;
}

bool qskIsAdjustableByLayout( const QQuickItem* item )
{
    return qskEffectivePlacementPolicy( item ) == QskPlacementPolicy::Adjust;
}

QskSizePolicy qskSizePolicy( const QQuickItem* item )
{
    if ( auto control = qskControlCast( item ) )
        return control->sizePolicy();

    if ( item )
    {
        const QVariant v = item->property( "sizePolicy" );
        if ( v.canConvert< QskSizePolicy >() )
            return qvariant_cast< QskSizePolicy >( v );
    }

    return QskSizePolicy( QskSizePolicy::Preferred, QskSizePolicy::Preferred );
}

Qt::Alignment qskLayoutAlignmentHint( const QQuickItem* item )
{
    if ( auto control = qskControlCast( item ) )
        return control->layoutAlignmentHint();

    if ( item )
    {
        const QVariant v = item->property( "layoutAlignmentHint" );
        if ( v.canConvert< Qt::Alignment >() )
            return v.value< Qt::Alignment >();
    }

    return Qt::Alignment();
}

void qskSetPlacementPolicy( QQuickItem* item, const QskPlacementPolicy policy )
{
    if ( item == nullptr )
        return;

    if ( auto control = qskControlCast( item ) )
    {
        control->setPlacementPolicy( policy );
    }
    else
    {
        item->setProperty( "layoutPolicy", QVariant::fromValue( policy ) );

        auto d = QQuickItemPrivate::get( item );

        const bool ignore = policy.visiblePolicy() == QskPlacementPolicy::Ignore;
        if ( ignore != d->isTransparentForPositioner() )
            d->setTransparentForPositioner( ignore );

        // sending a LayoutRequest ?
    }
}

QskPlacementPolicy qskPlacementPolicy( const QQuickItem* item )
{
    if ( item == nullptr )
        return QskPlacementPolicy( QskPlacementPolicy::Ignore, QskPlacementPolicy::Ignore );

    if ( auto control = qskControlCast( item ) )
    {
        return control->placementPolicy();
    }
    else
    {
        QskPlacementPolicy policy;

        const auto v = item->property( "layoutPolicy" );
        if ( v.canConvert< QskPlacementPolicy >() )
            policy = v.value< QskPlacementPolicy >();

        auto d = QQuickItemPrivate::get( item );
        if ( d->isTransparentForPositioner() )
            policy.setVisiblePolicy( QskPlacementPolicy::Ignore );

        return policy;
    }
}

QskPlacementPolicy::Policy qskEffectivePlacementPolicy( const QQuickItem* item )
{
    if ( item == nullptr )
        return QskPlacementPolicy::Ignore;

    const auto policy = qskPlacementPolicy( item );

    if ( qskIsVisibleToParent( item ) )
        return policy.visiblePolicy();
    else
        return policy.hiddenPolicy();
}

QQuickItem* qskNearestFocusScope( const QQuickItem* item )
{
    if ( item )
    {
        for ( auto scope = item->parentItem();
            scope != nullptr; scope = scope->parentItem() )
        {
            if ( scope->isFocusScope() )
                return scope;
        }

        /*
            As the default setting of the root item is to be a focus scope
            we usually never get here - beside the flag has been explicitely
            disabled in application code.
         */
    }

    return nullptr;
}

void qskForceActiveFocus( QQuickItem* item, Qt::FocusReason reason )
{
    /*
        For unknown reasons Qt::PopupFocusReason is blocked inside of
        QQuickItem::setFocus and so we can't use QQuickItem::forceActiveFocus
     */

    if ( item == nullptr || item->window() == nullptr )
        return;


#if QT_VERSION >= QT_VERSION_CHECK( 6, 1, 0 )
    auto wp = QQuickItemPrivate::get( item )->deliveryAgentPrivate();
#else
    auto wp = QQuickWindowPrivate::get( item->window() );
#endif
    while ( const auto scope = qskNearestFocusScope( item ) )
    {
        wp->setFocusInScope( scope, item, reason );
        item = scope;
    }
}

void qskUpdateInputMethod( const QQuickItem* item, Qt::InputMethodQueries queries )
{
    if ( item == nullptr || !( item->flags() & QQuickItem::ItemAcceptsInputMethod ) )
        return;

    static QPlatformInputContext* context = nullptr;
    static int methodId = -1;

    /*
        We could also get the inputContext from QInputMethodPrivate
        but for some reason the gcc sanitizer reports errors
        when using it. So let's go with QGuiApplicationPrivate.
     */

    auto inputContext = qskPlatformIntegration()->inputContext();
    if ( inputContext == nullptr )
    {
        context = nullptr;
        methodId = -1;

        return;
    }

    if ( inputContext != context )
    {
        context = inputContext;
        methodId = inputContext->metaObject()->indexOfMethod(
            "update(const QQuickItem*,Qt::InputMethodQueries)" );
    }

    if ( methodId >= 0 )
    {
        /*
            The protocol for input methods does not fit well for a
            virtual keyboard as it is tied to the focus.
            So we try to bypass QInputMethod calling the
            inputContext directly.
         */

        const auto method = inputContext->metaObject()->method( methodId );
        method.invoke( inputContext, Qt::DirectConnection,
            Q_ARG( const QQuickItem*, item ), Q_ARG( Qt::InputMethodQueries, queries ) );
    }
    else
    {
        QGuiApplication::inputMethod()->update( queries );
    }
}

void qskInputMethodSetVisible( const QQuickItem* item, bool on )
{
    static QPlatformInputContext* context = nullptr;
    static int methodId = -1;

    auto inputContext = qskPlatformIntegration()->inputContext();
    if ( inputContext == nullptr )
    {
        context = nullptr;
        methodId = -1;

        return;
    }

    if ( inputContext != context )
    {
        context = inputContext;
        methodId = inputContext->metaObject()->indexOfMethod(
            "setInputPanelVisible(const QQuickItem*,bool)" );
    }

    if ( methodId >= 0 )
    {
        const auto method = inputContext->metaObject()->method( methodId );
        method.invoke( inputContext, Qt::DirectConnection,
            Q_ARG( const QQuickItem*, item ), Q_ARG( bool, on ) );
    }
    else
    {
        if ( on )
            QGuiApplication::inputMethod()->show();
        else
            QGuiApplication::inputMethod()->hide();
    }
}

QList< QQuickItem* > qskPaintOrderChildItems( const QQuickItem* item )
{
    if ( item )
        return QQuickItemPrivate::get( item )->paintOrderChildItems();

    return QList< QQuickItem* >();
}

const QSGTransformNode* qskItemNode( const QQuickItem* item )
{
    if ( item == nullptr )
        return nullptr;

    return QQuickItemPrivate::get( item )->itemNodeInstance;
}

const QSGNode* qskPaintNode( const QQuickItem* item )
{
    if ( item == nullptr )
        return nullptr;

    return QQuickItemPrivate::get( item )->paintNode;
}

const QSGRootNode* qskScenegraphAnchorNode( const QQuickItem* item )
{
    if ( item == nullptr )
        return nullptr;

    return QQuickItemPrivate::get( item )->rootNode();
}

const QSGRootNode* qskScenegraphAnchorNode( const QQuickWindow* window )
{
    if ( auto w = const_cast< QQuickWindow* >( window ) )
    {
        if ( auto renderer = QQuickWindowPrivate::get( w )->renderer )
            return renderer->rootNode();
    }

    return nullptr;
}

void qskSetScenegraphAnchor( QQuickItem* item, bool on )
{
    /*
        For setting up a subtree renderer ( f.e in QskSceneTexture ) we need
        to insert a QSGRootNode above the paintNode.

        ( In Qt this feature is exlusively used in the Qt/Quick Effects module
        what lead to the not very intuitive name "refFromEffectItem" )

        refFromEffectItem also allows to insert a opacity node of 0 to
        hide the subtree from the main renderer by setting its parameter to
        true. We have QskItemNode to achieve the same.
     */
    if ( item )
    {
        auto d = QQuickItemPrivate::get( item );
        if ( on )
            d->refFromEffectItem( false );
        else
            d->derefFromEffectItem( false );
    }
}

QSizeF qskEffectiveSizeHint( const QQuickItem* item,
    Qt::SizeHint whichHint, const QSizeF& constraint )
{
    if ( auto control = qskControlCast( item ) )
        return control->effectiveSizeHint( whichHint, constraint );

    if ( constraint.width() >= 0.0 || constraint.height() >= 0.0 )
    {
        // QQuickItem does not support dynamic constraints
        return constraint;
    }

    if ( item == nullptr )
        return QSizeF();

    /*
        Trying to retrieve something useful for non QskControls:

        First are checking some properties, that usually match the
        names for the explicit hints. For the implicit hints we only
        have the implicitSize, what is interpreted as the implicit
        preferred size.
     */
    QSizeF hint;

    static const char* properties[] =
    {
        "minimumSize",
        "preferredSize",
        "maximumSize"
    };

    const auto v = item->property( properties[ whichHint ] );
    if ( v.canConvert< QSizeF >() )
        hint = v.toSizeF();

    if ( whichHint == Qt::PreferredSize )
    {
        if ( hint.width() < 0 )
            hint.setWidth( item->implicitWidth() );

        if ( hint.height() < 0 )
            hint.setHeight( item->implicitHeight() );
    }

    return hint;
}

QSizeF qskSizeConstraint( const QQuickItem* item,
    Qt::SizeHint which, const QSizeF& constraint )
{
    if ( item == nullptr )
        return QSizeF( 0, 0 );

    const QskItemLayoutElement layoutElement( item );
    return layoutElement.sizeConstraint( which, constraint );
}

QSizeF qskConstrainedItemSize( const QQuickItem* item, const QSizeF& size )
{
    if ( item == nullptr )
        return QSizeF( 0.0, 0.0 );

    const QskItemLayoutElement layoutElement( item );
    return layoutElement.constrainedSize( size );
}

QRectF qskConstrainedItemRect( const QQuickItem* item,
    const QRectF& rect, Qt::Alignment alignment )
{
    const auto size = qskConstrainedItemSize( item, rect.size() );
    return qskAlignedRectF( rect, size, alignment );
}

void qskItemUpdateRecursive( QQuickItem* item )
{
    if ( item == nullptr )
        return;

    if ( item->flags() & QQuickItem::ItemHasContents )
        item->update();

    const auto& children = QQuickItemPrivate::get( item )->childItems;
    for ( auto child : children )
        qskItemUpdateRecursive( child );
}

#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )

static const QQuickPointerTouchEvent* qskPointerPressEvent( const QQuickWindowPrivate* wd )
{
    for ( const auto event : std::as_const( wd->pointerEventInstances ) )
    {
        if ( auto touchEvent = event->asPointerTouchEvent() )
        {
            if ( touchEvent->isPressEvent() )
                return touchEvent;
        }
    }

    return nullptr;
}

#endif

bool qskGrabMouse( QQuickItem* item )
{
    if ( item == nullptr || item->window() == nullptr )
        return false;

    if ( const auto mouseGrabber = item->window()->mouseGrabberItem() )
    {
        if ( mouseGrabber == item )
            return true;

        if ( mouseGrabber->keepMouseGrab() )
        {
            // we respect this
            return false;
        }
    }

    item->setKeepMouseGrab( true );

#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )

    auto wd = QQuickWindowPrivate::get( item->window() );
    if ( wd->touchMouseDevice == nullptr )
    {
        /*
            For synthesized mouse events QQuickWindow sends
            an initial QEvent::MouseButtonPress before setting
            touchMouseDevice/touchMouseId. As the mouse grabber is
            stored depending on these attributes the following
            mouse event callbacks will look for the grabber at a
            a different place as it was stored.
         */

        if ( const auto event = qskPointerPressEvent( wd ) )
        {
            if ( const auto p = event->point( 0 ) )
            {
                wd->touchMouseDevice = event->device();
                wd->touchMouseId = p->pointId();

                item->grabMouse();

                wd->touchMouseDevice = nullptr;
                wd->touchMouseId = -1;

                return true;
            }
        }
    }
#endif

    item->grabMouse();
    return true;
}

void qskUngrabMouse( QQuickItem* item )
{
    if ( item )
    {
        item->setKeepMouseGrab( false );

        if ( qskIsMouseGrabber( item ) )
            item->ungrabMouse();
    }
}

bool qskIsMouseGrabber( const QQuickItem* item )
{
    if ( item )
    {
        if ( const auto window = item->window() )
            return window->mouseGrabberItem() == item;
    }

    return false;
}
