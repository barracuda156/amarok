/****************************************************************************************
 * Copyright (c) 2007 Leo Franchi <lfranchi@gmail.com>                                  *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) any later           *
 * version.                                                                             *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#ifndef AMAROK_APPLET_H
#define AMAROK_APPLET_H

#include "amarok_export.h"

#include <plasma/applet.h>

#include <QFont>
#include <QRectF>
#include <QString>
#include <QWeakPointer>

class QPainter;
class QPropertyAnimation;

namespace Plasma
{
    class FrameSvg;
    class IconWidget;
}

namespace Context
{

class AMAROK_EXPORT Applet : public Plasma::Applet
{
    Q_OBJECT
    Q_PROPERTY(qreal animate READ animationValue WRITE animate)
    public:
        explicit Applet( QObject* parent, const QVariantList& args = QVariantList() );
        ~Applet();

        /**
         * Return a QFont that will allow the given text to fit within the rect.
         */
        QFont shrinkTextSizeToFit( const QString& text, const QRectF& bounds );

        /**
         * Truncate the text by adding an ellipsis at the end in order to make the text with the given font
         * rit in the bounding rect.
         */
        QString truncateTextToFit( const QString &text, const QFont& font, const QRectF& bounds );

        void paintInterface( QPainter *p, const QStyleOptionGraphicsItem *option, const QRect &contentsRect );

        /**
          * Returns a standard CV-wide padding that applets can use for consistency.
          */
        qreal standardPadding();

        /**
          * Collapse animation
          */
        void setCollapseOn();
        void setCollapseOff();
        void setCollapseHeight( int );

        bool isAppletCollapsed();
        bool isAppletExtended();

        /**
          * sizeHint is reimplemented here only for all the applet.
          */
        virtual QSizeF sizeHint( Qt::SizeHint which, const QSizeF & constraint = QSizeF() ) const;

        /**
          * resize is reimplemented here is reimplemented here only for all the applet.
          */
        virtual void   resize( qreal, qreal );

        /**
          * Returns the current animation value.
          */
        qreal animationValue() const;

    public Q_SLOTS:
        virtual void destroy();

    protected slots:
        void animate( qreal );
        void animateEnd();

    private slots:
        void paletteChanged( const QPalette & palette );

    protected:
        /**
         * Paint the background of an applet, so it fits with all the other applets.
         *  Background is *no longer a gradient*. However, please use this to
         *  stay consistent with other applets.
         */
        void addGradientToAppletBackground( QPainter* p );

        Plasma::IconWidget* addAction( QAction *action, const int size = 16 );
        bool canAnimate();

        bool m_canAnimate;
        bool m_collapsed;
        int  m_heightCurrent;
        int  m_heightCollapseOn;
        int  m_heightCollapseOff;
        int  m_animFromHeight;

    private:
        void cleanUpAndDelete();

        bool m_transient;
        qreal m_standardPadding;
        Plasma::FrameSvg *m_textBackground;
        QWeakPointer<QPropertyAnimation> m_animation;
};

} // Context namespace

/**
 * Register an applet when it is contained in a loadable module
 */
#define K_EXPORT_AMAROK_APPLET(libname, classname) \
K_PLUGIN_FACTORY(factory, registerPlugin<classname>();) \
K_EXPORT_PLUGIN(factory("amarok_context_applet_" #libname))\
K_EXPORT_PLUGIN_VERSION(PLASMA_VERSION)

#endif // multiple inclusion guard
