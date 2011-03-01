/****************************************************************************************
 * Copyright (c) 2008 Daniel Caleb Jones <danielcjones@gmail.com>                       *
 * Copyright (c) 2009 Mark Kretschmann <kretschmann@kde.org>                            *
 * Copyright (c) 2010,2011 Ralf Engels <ralf-engels@gmx.de>                             *
 *                                                                                      *
 * This program is free software; you can redistribute it and/or modify it under        *
 * the terms of the GNU General Public License as published by the Free Software        *
 * Foundation; either version 2 of the License, or (at your option) version 3 or        *
 * any later version accepted by the membership of KDE e.V. (or its successor approved  *
 * by the membership of KDE e.V.), which shall act as a proxy defined in Section 14 of  *
 * version 3 of the license.                                                            *
 *                                                                                      *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY      *
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A      *
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.             *
 *                                                                                      *
 * You should have received a copy of the GNU General Public License along with         *
 * this program.  If not, see <http://www.gnu.org/licenses/>.                           *
 ****************************************************************************************/

#include "DynamicBiasWidgets.h"

#include "Bias.h"
#include "BiasFactory.h"
#include "biases/TagMatchBias.h"
#include "biases/PartBias.h"
#include "core/support/Debug.h"
#include "SliderWidget.h"
#include "SvgHandler.h"
#include "widgets/MetaQueryWidget.h"

#include <QLabel>
#include <QSlider>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>

#include <KComboBox>
#include <KIcon>
#include <KVBox>
#include <klocale.h>

PlaylistBrowserNS::BiasDialog::BiasDialog( Dynamic::BiasPtr bias, QWidget* parent )
    : QDialog( parent )
    , m_mainLayout( 0 )
    , m_descriptionLabel( 0 )
    , m_biasWidget( 0 )
    , m_bias( bias )
{
    m_mainLayout = new QVBoxLayout( this );

    // -- the bias selection combo
    QLabel* selectionLabel = new QLabel( i18nc("Bias selection label in bias view.", "Match Type:" ) );
    m_biasSelection = new KComboBox( this );
    factoriesChanged();
    connect( Dynamic::BiasFactory::instance(), SIGNAL( changed() ),
             this, SLOT( factoriesChanged() ) );

    connect( m_biasSelection, SIGNAL( activated( int ) ),
             this, SLOT( selectionChanged( int ) ) );

    QHBoxLayout *selectionLayout = new QHBoxLayout();
    selectionLabel->setBuddy( m_biasSelection );
    selectionLayout->addWidget( selectionLabel );
    selectionLayout->addWidget( m_biasSelection );
    m_mainLayout->addLayout( selectionLayout );

    // -- bias itself
    m_descriptionLabel = new QLabel( "" );
    m_mainLayout->addWidget( m_descriptionLabel );

    m_biasWidget = new QWidget();
    m_mainLayout->addWidget( m_biasWidget );

    // -- button box
    QDialogButtonBox* buttonBox = new QDialogButtonBox( QDialogButtonBox::Ok );
    m_mainLayout->addWidget( buttonBox );

    connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));

    biasReplaced( Dynamic::BiasPtr(), bias );
}

void
PlaylistBrowserNS::BiasDialog::factoriesChanged()
{
    m_biasSelection->clear();

    // -- add all the bias types to the list
    bool factoryFound = false;
    QList<Dynamic::AbstractBiasFactory*> factories = Dynamic::BiasFactory::factories();
    for( int i = 0; i <  factories.count(); i++ )
    {
        Dynamic::AbstractBiasFactory* factory = factories.at( i );
        m_biasSelection->addItem( factory->i18nName(), QVariant( factory->name() ) );

        // -- set the current index if we have found our own factory
        if( m_bias && factory->name() == m_bias->name() )
        {
            factoryFound = true;
            m_biasSelection->setCurrentIndex( i );
            // while we are at it: set a tool tip
            setToolTip( factory->i18nDescription() );
        }
    }

    // -- In cases of replacement bias
    if( !factoryFound )
    {
        m_biasSelection->addItem( m_bias->name() );
        m_biasSelection->setCurrentIndex( m_biasSelection->count() );
        setToolTip( i18n( "Replacement for %1 bias" ).arg( m_bias->name() ) );
    }
}

void
PlaylistBrowserNS::BiasDialog::selectionChanged( int index )
{
    Q_ASSERT( m_biasSelection );

    QString biasName = m_biasSelection->itemData( index ).toString();

    Dynamic::BiasPtr oldBias( m_bias );
    Dynamic::BiasPtr newBias( Dynamic::BiasFactory::fromName( biasName ) );
    if( !newBias )
    {
        warning() << "Could not create bias with name:"<<biasName;
        return;
    }

    m_bias->replace( newBias ); // tell the old bias it has just been replaced

    // -- if the new bias is AndBias, try to add the old biase(s) into it
    Dynamic::AndBias *oldABias = qobject_cast<Dynamic::AndBias*>(oldBias.data());
    Dynamic::AndBias *newABias = qobject_cast<Dynamic::AndBias*>(newBias.data());
    if( newABias ) {
        if( oldABias ) {
            for( int i = 0; i < oldABias->biases().count(); i++ )
            {
                // skip the default random bias of the PartBias
                if( i > 0 || !qobject_cast<Dynamic::PartBias*>(oldABias) )
                    newABias->appendBias( oldABias->biases()[i] );
            }
        }
        else
        {
            newABias->appendBias( oldBias );
        }
        // TODO: the and bias automatically adds a random bias that should be removed.
        //       the part bias adds a random bias that should not be removed.
    }
}

void
PlaylistBrowserNS::BiasDialog::biasReplaced( Dynamic::BiasPtr oldBias, Dynamic::BiasPtr newBias )
{
    Q_UNUSED( oldBias );

    if( m_biasWidget )
    {
        m_biasWidget->deleteLater();
        m_biasWidget = 0;
    }

    m_bias = newBias;
    if( !newBias )
        return;

    connect( newBias.data(), SIGNAL( replaced( Dynamic::BiasPtr, Dynamic::BiasPtr ) ),
             this, SLOT( biasReplaced( Dynamic::BiasPtr, Dynamic::BiasPtr ) ) );

    m_biasWidget = newBias->widget( 0 );
    if( !m_biasWidget )
        m_biasWidget = new QLabel( i18n("This bias has no settings") );

    m_mainLayout->takeAt( 2 );
    m_mainLayout->insertWidget( 2, m_biasWidget );
}


// -------- PartBiasWidget -----------


PlaylistBrowserNS::PartBiasWidget::PartBiasWidget( Dynamic::PartBias* bias, QWidget* parent )
    : QWidget( parent )
    , m_inSignal( false )
    , m_bias( bias )
{
    connect( bias, SIGNAL( biasAppended( Dynamic::BiasPtr ) ),
             this, SLOT( biasAppended( Dynamic::BiasPtr ) ) );

    connect( bias, SIGNAL( biasRemoved( int ) ),
             this, SLOT( biasRemoved( int ) ) );

    connect( bias, SIGNAL( biasMoved( int, int ) ),
             this, SLOT( biasMoved( int, int ) ) );

    connect( this, SIGNAL( biasWeightChanged( int, qreal ) ),
             bias, SLOT( changeBiasWeight( int, qreal ) ) );
    connect( bias, SIGNAL( weightsChanged() ),
             this, SLOT( biasWeightsChanged() ) );

    m_layout = new QGridLayout( this );

    // -- add all sub-bias widgets
    foreach( Dynamic::BiasPtr bias, m_bias->biases() )
    {
        biasAppended( bias );
    }
}

void
PlaylistBrowserNS::PartBiasWidget::appendBias()
{
    m_bias->appendBias( Dynamic::BiasPtr( new Dynamic::TagMatchBias() ) );
}

void
PlaylistBrowserNS::PartBiasWidget::biasAppended( Dynamic::BiasPtr bias )
{
    int index = m_bias->biases().indexOf( bias );

    Amarok::Slider* slider = 0;
    slider = new Amarok::Slider( Qt::Horizontal, 100, this );
    slider->setValue( m_bias->weights()[ m_bias->biases().indexOf( bias ) ] * 100.0 );
    slider->setToolTip( i18n( "This controls what portion of the playlist should match the criteria" ) );
    connect( slider, SIGNAL(valueChanged(int)), SLOT(sliderValueChanged(int)) );
    m_layout->addWidget( slider, index, 0 );

    // -- add the widget (with slider)

    QLabel* label = new QLabel( bias->toString() );
    m_layout->addWidget( label, index, 1 );
}

void
PlaylistBrowserNS::PartBiasWidget::biasRemoved( int pos )
{
    m_layout->takeAt( pos * 2 );
    m_layout->takeAt( pos * 2 );
    m_sliders.takeAt( pos );
    m_widgets.takeAt( pos );
}

void
PlaylistBrowserNS::PartBiasWidget::biasMoved( int from, int to )
{
    QSlider* slider = m_sliders.takeAt( from );
    m_sliders.insert( to, slider );

    QWidget* widget = m_widgets.takeAt( from );
    m_widgets.insert( to, widget );

    // -- move the item in the layout
    // TODO
    /*
    m_layout->insertWidget( to * 2, slider );
    m_layout->insertWidget( to * 2 + 1, widget );
    */
}

void
PlaylistBrowserNS::PartBiasWidget::sliderValueChanged( int val )
{
    // protect agains recursion
    if( m_inSignal )
        return;

    for( int i = 0; i < m_sliders.count(); i++ )
    {
        if( m_sliders.at(i) == sender() )
            emit biasWeightChanged( i, qreal(val) / 100.0 );
    }
}

void
PlaylistBrowserNS::PartBiasWidget::biasWeightsChanged()
{
    // protect agains recursion
    if( m_inSignal )
        return;

    m_inSignal = true;

    QList<qreal> weights = m_bias->weights();
    for( int i = 0; i < weights.count(); i++ )
        m_sliders.at(i)->setValue( weights.at(i) * 100.0 );

    m_inSignal = false;
}


// ---------- TagMatchBias --------


PlaylistBrowserNS::TagMatchBiasWidget::TagMatchBiasWidget( Dynamic::TagMatchBias* bias,
                                                           QWidget* parent )
    : QWidget( parent )
    , m_bias( bias )
{
    QHBoxLayout *layout = new QHBoxLayout( this );
    m_queryWidget = new MetaQueryWidget();
    /*
    m_queryWidget->setSizePolicy( QSizePolicy( QSizePolicy::MinimumExpanding,
                                               QSizePolicy::Preferred ) );

    layout->addRow( i18nc("Tag Match Bias selection label in bias view. Try to keep below 15 characters or abbreviate", "Match:" ), m_queryWidget );

*/
    syncControlsToBias();

    connect( m_queryWidget, SIGNAL(changed(const MetaQueryWidget::Filter&)),
             SLOT(syncBiasToControls()));

    layout->addWidget( m_queryWidget );
}

void
PlaylistBrowserNS::TagMatchBiasWidget::syncControlsToBias()
{
    m_queryWidget->setFilter( m_bias->filter() );
}

void
PlaylistBrowserNS::TagMatchBiasWidget::syncBiasToControls()
{
    m_bias->setFilter( m_queryWidget->filter() );
}


#include "DynamicBiasWidgets.moc"
