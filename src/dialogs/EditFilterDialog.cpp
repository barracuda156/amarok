/****************************************************************************************
 * Copyright (c) 2006 Giovanni Venturi <giovanni@kde-it.org>                            *
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

#include "EditFilterDialog.h"

#define DEBUG_PREFIX "EditFilterDialog"

#include "amarokconfig.h"
#include "core/support/Debug.h"
#include "core-impl/collections/support/CollectionManager.h"
#include "core/collections/MetaQueryMaker.h"

#include <KGlobal>
#include <KLineEdit>
#include <KLocale>
#include <KMessageBox>

#include <QLabel>
#include <QLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>


EditFilterDialog::EditFilterDialog( QWidget* parent, const QString &text )
    : KDialog( parent )
    , m_appended( false )
    , m_filterText( text )
{
    setCaption( i18n( "Edit Filter" ) );
    setModal( true );
    setButtons( User1|User2|Default|Ok|Cancel );
    setDefaultButton( Cancel );
    showButtonSeparator( true );
    m_ui.setupUi( mainWidget() );
    setMinimumSize( minimumSizeHint() );

    // Redefine "Default" button
    KGuiItem defaultButton( i18n("&Append"), "list-add" );
    setButtonWhatsThis( Default, i18n( "<qt><p>By clicking here you can add the defined condition. The \"OK\" button will "
                                        "close the dialog and apply the defined filter. With this button you can add more than "
                                        "one condition to create a more complex filtering condition.</p></qt>" ) );
    setButtonToolTip( Default, i18n( "Add this filter condition to the list" ) );
    setButtonGuiItem( Default, defaultButton );

    // define "User1" button
    KGuiItem user1Button( i18n("&Clear"), "list-remove" );
    setButtonWhatsThis( User1, i18n( "<p>By clicking here you will clear the filter. If you intend to "
                                     "undo the last appending just click on the \"Undo\" button.</p>" ) );
    setButtonToolTip(User1, i18n( "Clear the filter" ) );
    setButtonGuiItem( User1, user1Button );

    // define "User2" button
    KGuiItem user2Button( i18nc("this \"undo\" will undo the last appended filter... be careful how you will translate it "
       "to avoid two buttons (\"Cancel\" and \"Undo\") with same label in the same dialog", "&Undo"), "edit-undo" );
    setButtonWhatsThis( User2, i18n( "<p>Clicking here will remove the last appended filter. "
                "You cannot undo more than one action.</p>" ) );
    setButtonToolTip( User2, i18n( "Remove last appended filter" ) );
    setButtonGuiItem( User2, user2Button );
    
    connect( m_ui.keywordCombo, SIGNAL(currentIndexChanged(const QString&)),
                                SLOT(selectedAttribute(const QString&)) );

    connect( m_ui.minimum, SIGNAL(valueChanged(int)), SLOT(minSpinChanged(int)) );
    connect( m_ui.maximum, SIGNAL(valueChanged(int)), SLOT(maxSpinChanged(int)) );

    // type text selected
    textWanted();

    // check the "One Value Choosing" by default
    chooseOneValue();

    connect( m_ui.conditionCombo, SIGNAL(activated( int ) ), SLOT(chooseCondition( int ) ) );

    m_checkActions << m_ui.matchAll;
    m_checkActions << m_ui.matchAny;
    m_checkActions << m_ui.matchLiteral;
    m_checkActions << m_ui.matchNot;

    // check "select all words" as default
    m_ui.matchAll->setChecked( true );

    m_ui.invertButton->setEnabled( false );

    // you need to append at least one filter condition to specify if do
    // an "AND" or an "OR" with the next condition if the filter is empty
    //
    if( m_filterText.isEmpty() )
    {
        m_ui.andButton->setEnabled( false );
        m_ui.orButton->setEnabled( false );
    }

    // check "AND" condition as default
    m_ui.andButton->setChecked( true );

    connect( this, SIGNAL(okClicked()), this, SLOT(slotOk()) );
    connect( this, SIGNAL(defaultClicked()) , this, SLOT(slotDefault()) );
    connect( this, SIGNAL(user1Clicked()), this, SLOT(slotUser1()) );
    connect( this, SIGNAL(user2Clicked()), this, SLOT(slotUser2()) );
    
    Collections::Collection *coll = CollectionManager::instance()->primaryCollection();
    if( !coll )
        return;

    Collections::QueryMaker *artist = coll->queryMaker()->setQueryType( Collections::QueryMaker::Artist );
    Collections::QueryMaker *album = coll->queryMaker()->setQueryType( Collections::QueryMaker::Album );
    Collections::QueryMaker *composer = coll->queryMaker()->setQueryType( Collections::QueryMaker::Composer );
    Collections::QueryMaker *genre = coll->queryMaker()->setQueryType( Collections::QueryMaker::Genre );
    QList<Collections::QueryMaker*> queries;
    queries << artist << album << composer << genre;

    //MetaQueryMaker will run multiple different queries just fine as long as we do not use it
    //to set the query type. Configuring the queries is ok though

    Collections::MetaQueryMaker *dataQueryMaker = new Collections::MetaQueryMaker( queries );
    connect( dataQueryMaker, SIGNAL( newResultReady( QString, Meta::ArtistList ) ), SLOT( resultReady( QString, Meta::ArtistList ) ), Qt::QueuedConnection );
    connect( dataQueryMaker, SIGNAL( newResultReady( QString, Meta::AlbumList ) ), SLOT( resultReady( QString, Meta::AlbumList ) ), Qt::QueuedConnection );
    connect( dataQueryMaker, SIGNAL( newResultReady( QString, Meta::ComposerList ) ), SLOT( resultReady( QString, Meta::ComposerList ) ), Qt::QueuedConnection );
    connect( dataQueryMaker, SIGNAL( newResultReady( QString, Meta::GenreList ) ), SLOT( resultReady( QString, Meta::GenreList ) ), Qt::QueuedConnection );
    dataQueryMaker->setAutoDelete( true );
    dataQueryMaker->run();
    
}

EditFilterDialog::~EditFilterDialog()
{
    delete m_ui.editKeywordBox;
}

QString EditFilterDialog::filter() const
{
    return m_filterText;
}

QString EditFilterDialog::keywordConditionText( const QString& keyword ) const
{
    const bool toInvert = m_ui.invertButton->isChecked();
    QString result = keyword;
    if( toInvert )
        result.prepend( QChar('-') );
    return result;
}

QString EditFilterDialog::keywordConditionNumeric( const QString& keyword ) const
{
    // this member is called when there is a keyword that needs numeric attributes
    QString result;

    const int minVal = m_ui.minimum->value();
    const int maxVal = m_ui.maximum->value();
    const QString &condition = m_ui.conditionCombo->currentText();
    const bool toInvert = m_ui.invertButton->isChecked();

    if( condition.compare( i18n("Equal To") ) == 0 )
    {
        result = keyword + ":" + QString::number( minVal );
        if( toInvert )
            result.prepend( QChar('-') );
    }
    else if( condition.compare( i18n("Smaller Than") ) == 0 )
    {
        result = keyword + ":<" + QString::number( minVal );
        if( toInvert )
            result.prepend( QChar('-') );
    }
    else if( condition.compare( i18n("Larger Than") ) == 0 )
    {
        result = keyword + ":>" + QString::number( minVal );
        if( toInvert )
            result.prepend( QChar('-') );
    }
    else if( condition.compare( i18n("Between") ) == 0 )
    {
        result = QString( "%1:%2%3 %4:%5%6" )
            .arg( keyword ).arg( toInvert ? QChar('<') : QChar('>') ).arg( QString::number(minVal - 1) )
            .arg( keyword ).arg( toInvert ? QChar('>') : QChar('<') ).arg( QString::number(maxVal + 1) );
    }
    return result;
}

// SLOTS
void EditFilterDialog::selectedAttribute( const QString &attr ) // SLOT
{
    debug() << QString( "Attribute '%1' selected: '%2'" ).arg( attr ).arg( m_ui.keywordCombo->currentText() );
    m_ui.filterActionGroupBox->setEnabled( false );
    m_ui.invertButton->setEnabled( true );

    if( attr.compare( i18n("Simple Search") ) == 0 )
    {
        m_ui.filterActionGroupBox->setEnabled( true );
        m_ui.invertButton->setEnabled( false );
        textWanted();
    }
    else if( attr.compare( i18n("Bit Rate") ) == 0 )
    {
        m_ui.minimum->setValue( 128 );
        m_ui.maximum->setValue( 384 );
        valueWanted();
    }
    else if( attr.compare( i18n("Sample Rate") ) == 0 )
    {
        m_ui.minimum->setValue( 8000 );
        m_ui.maximum->setValue( 48000 );
        valueWanted();
    }
    else if( attr.compare( i18n("File Size") ) == 0 ) // TODO: add to ui
    {
        m_ui.minimum->setValue( 0 );
        m_ui.maximum->setValue( 200000 );
        valueWanted();
    }
    else if( attr.compare( i18n("Year") ) == 0 )
    {
        m_ui.minimum->setValue( QDate::currentDate().year() );
        m_ui.maximum->setValue( QDate::currentDate().year() );
        valueWanted();
    }
    else if( attr.compare( i18n("BPM") ) == 0 )
    {
        m_ui.minimum->setValue( 60 );
        m_ui.maximum->setValue( 120 );
        valueWanted();
    }
    else if( attr.compare( i18n("Track Number") ) == 0 )
    {
        m_ui.minimum->setValue( 1 );
        m_ui.maximum->setValue( 100 );
        valueWanted();
    }
    else if( attr.compare( i18n("Track Length") ) == 0 )
    {
        m_ui.minimum->setValue( 0 );
        m_ui.maximum->setValue( 3600 );
        valueWanted();
    }
    else if( attr.compare( i18n("Disc Number") ) == 0 )
    {
        m_ui.minimum->setValue( 1 );
        m_ui.maximum->setValue( 10 );
        valueWanted();
    }
    else if( attr.compare( i18n("Playcount") ) == 0 )
    {
        m_ui.minimum->setValue( 0 );
        m_ui.maximum->setValue( 1000 );
        valueWanted();
    }
    else if( attr.compare( i18n("Score") ) == 0 )
    {
        m_ui.minimum->setValue( 0 );
        m_ui.maximum->setValue( 100 );
        valueWanted();
    }
    else if( attr.compare( i18n("Rating") ) == 0 )
    {
        m_ui.minimum->setValue( 0 );
        m_ui.maximum->setValue( 10 );
        valueWanted();
    }
    else if( attr.compare( i18n("Last Played") ) == 0 ) // TODO: add to ui
    {
        valueWanted();
    }
    //FIXME: PORT 2.0
//     else if( key=="label" )
//         textWanted( CollectionDB::instance()->labelList() );
    else if( attr.compare( i18n("Album") ) == 0 )
    {
        textWanted( m_albums );
    }
    else if( attr.compare( i18n("Artist") ) == 0 )
    {
        textWanted( m_artists );
    }
    else if( attr.compare( i18n("Composer") ) == 0 )
    {
        textWanted( m_composers );
    }
    else if( attr.compare( i18n("Genre") ) == 0 )
    {
        textWanted( m_genres );
    }
    else if( attr.compare( i18n("Track Title") ) == 0 )
    {
        textWanted();
    }
    else if( attr.compare( i18n("Format") ) == 0 )
    {
        QStringList types;
        types << "mp3" << "flac" << "ogg" << "mp4";
        textWanted( types );
    }
    else
        textWanted();
}

void EditFilterDialog::minSpinChanged( int value ) // SLOT
{
    if( value > m_ui.maximum->value() )
        m_ui.maximum->setValue( value );
}

void EditFilterDialog::maxSpinChanged( int value ) // SLOT
{
    if( m_ui.minimum->value() > value )
        m_ui.minimum->setValue( value );
}

void EditFilterDialog::textWanted() // SLOT
{
    m_ui.editKeywordBox->setEnabled( true );
    m_ui.valueGroupBox->setEnabled( false );

    m_ui.editKeywordBox->completionObject()->clear();
}

void EditFilterDialog::textWanted( const QStringList &completions ) // SLOT
{
    textWanted();

    m_ui.editKeywordBox->completionObject()->insertItems( completions );
    m_ui.editKeywordBox->completionObject()->setIgnoreCase( true );
    m_ui.editKeywordBox->setCompletionMode( KGlobalSettings::CompletionPopup );
}

void EditFilterDialog::valueWanted() // SLOT
{
    m_ui.editKeywordBox->setEnabled( false );
    m_ui.valueGroupBox->setEnabled( true );
}

void EditFilterDialog::chooseCondition( int condition ) // SLOT
{
    if( condition == 3 ) // included between
        chooseMinMaxValue();
    else
        chooseOneValue();
}

void EditFilterDialog::chooseOneValue() // SLOT
{
    m_ui.andLabel->setEnabled( false);
    m_ui.maximum->setEnabled( false );
}

void EditFilterDialog::chooseMinMaxValue() // SLOT
{
    m_ui.andLabel->setEnabled( true );
    m_ui.maximum->setEnabled( true );
}

void EditFilterDialog::slotDefault() // SLOT
{
    const QString &attr = m_ui.keywordCombo->currentText();

    // now append the filter rule if not empty
    if( m_ui.editKeywordBox->text().isEmpty() && (attr.compare(i18n("Simple Search")) == 0) )
    {
        KMessageBox::sorry( 0, i18n("<p>Sorry but the filter rule cannot be set. The text field is empty. "
                    "Please type something into it and retry.</p>"), i18n("Empty Text Field"));
        m_ui.editKeywordBox->setFocus();
        return;
    }

    if( !m_appended )
    {
        // it's the first rule
        m_appended = true;
        m_ui.andButton->setEnabled( true );
        m_ui.orButton->setEnabled( true );
    }

    m_previousFilterText = m_filterText;
    if( !m_filterText.isEmpty() )
    {
        m_filterText += ' ';
        if( m_ui.orButton->isChecked() )
            m_filterText += "OR ";
    }

    QStringList list = m_ui.editKeywordBox->text().split( ' ' );
    if( attr.compare( i18n("Simple Search") ) == 0 )
    {
        // Simple Search
        debug() << "selected text: '" << m_ui.editKeywordBox->text() << "'";
        if (m_checkActions[0]->isChecked())
        {
            // all words
            m_filterText += m_ui.editKeywordBox->text();
        }
        else if (m_checkActions[1]->isChecked())
        {
            // at least one word
            m_filterText += *(list.constBegin());
            for ( QStringList::ConstIterator it = ++list.constBegin(), end = list.constEnd(); it != end; ++it )
                m_filterText += " OR " + *it;
        }
        else if (m_checkActions[2]->isChecked())
        {
            // exactly the words
            m_filterText += "\"" + m_ui.editKeywordBox->text() + "\"";
        }
        else if (m_checkActions[3]->isChecked())
        {
            // exclude words
            for ( QStringList::ConstIterator it = list.constBegin(), end = list.constEnd(); it != end; ++it )
                m_filterText += " -" + *it;
        }
    }
    else if( attr.compare( i18n("Album") ) == 0 )
    {
        m_filterText += QString( "%1:\"%2\"" )
            .arg( keywordConditionText(i18n("album")) ).arg( m_ui.editKeywordBox->text() );
    }
    else if( attr.compare( i18n("Artist") ) == 0 )
    {
        m_filterText += QString( "%1:\"%2\"" )
            .arg( keywordConditionText(i18n("artist")) ).arg( m_ui.editKeywordBox->text() );
    }
    else if( attr.compare( i18n("Composer") ) == 0 )
    {
        m_filterText += QString( "%1:\"%2\"" )
            .arg( keywordConditionText(i18n("composer")) ).arg( m_ui.editKeywordBox->text() );
    }
    else if( attr.compare( i18n("Genre") ) == 0 )
    {
        m_filterText += QString( "%1:\"%2\"" )
            .arg( keywordConditionText(i18n("genre")) ).arg( m_ui.editKeywordBox->text() );
    }
    else if( attr.compare( i18n("Track Title") ) == 0 )
    {
        m_filterText += QString( "%1:\"%2\"" )
            .arg( keywordConditionText(i18n("title")) ).arg( m_ui.editKeywordBox->text() );
    }
    else if( attr.compare( i18n("Format") ) == 0 )
    {
        m_filterText += QString( "%1:\"%2\"" )
            .arg( keywordConditionText(i18n("format")) ).arg( m_ui.editKeywordBox->text() );
    }
    else if( attr.compare( i18n("Comment") ) == 0 )
    {
        m_filterText += QString( "%1:\"%2\"" )
            .arg( keywordConditionText(i18n("comment")) ).arg( m_ui.editKeywordBox->text() );
    }
    else if( attr.compare( i18n("Track Length") ) == 0 )
    {
        m_filterText += keywordConditionNumeric( i18n("length") );
    }
    else if( attr.compare( i18n("Track Number") ) == 0 )
    {
        m_filterText += keywordConditionNumeric( i18n("tracknumber") );
    }
    else if( attr.compare( i18n("Disc Number") ) == 0 )
    {
        m_filterText += keywordConditionNumeric( i18n("discnumber") );
    }
    else if( attr.compare( i18n("Bit Rate") ) == 0 )
    {
        m_filterText += keywordConditionNumeric( i18n("bitrate") );
    }
    else if( attr.compare( i18n("Sample Rate") ) == 0 )
    {
        m_filterText += keywordConditionNumeric( i18n("samplerate") );
    }
    else
    {
        m_filterText += keywordConditionNumeric( attr );
    }
    emit filterChanged( m_filterText );

    m_ui.editKeywordBox->clear();
}

void EditFilterDialog::slotUser1() // SLOT
{
    m_previousFilterText = m_filterText;
    m_filterText = "";

    // no filter appended cause all cleared
    m_appended = false;
    m_ui.andButton->setEnabled( false );
    m_ui.orButton->setEnabled( false );

    emit filterChanged( m_filterText );
}

void EditFilterDialog::slotUser2() // SLOT
{
    m_filterText = m_previousFilterText;
    if (m_filterText.isEmpty())
    {
        // no filter appended cause all cleared
        m_appended = false;
        m_ui.andButton->setEnabled( false );
        m_ui.orButton->setEnabled( false );
    }
    emit filterChanged( m_filterText );
}

void EditFilterDialog::slotOk() // SLOT
{
    // If there's a filter typed in but unadded, add it.
    // This makes it easier to just add one condition - you only need to press OK.
    if ( !m_ui.editKeywordBox->text().isEmpty() )
        slotDefault();

    // Don't let OK do anything if they haven't set any filters.
    if (m_appended)
        accept();
}

void
EditFilterDialog::resultReady( const QString &collectionId, const Meta::AlbumList &albums )
{
    Q_UNUSED( collectionId )
    foreach( Meta::AlbumPtr album, albums )
    {
        m_albums << album->name();
    }
}

void
EditFilterDialog::resultReady( const QString &collectionId, const Meta::ArtistList &artists )
{
    Q_UNUSED( collectionId )
    foreach( Meta::ArtistPtr artist, artists )
    {
        m_artists << artist->name();
    }
}

void
EditFilterDialog::resultReady( const QString &collectionId, const Meta::ComposerList &composers )
{
    Q_UNUSED( collectionId )
    foreach( Meta::ComposerPtr composer, composers )
    {
        m_composers << composer->name();
    }
}

void
EditFilterDialog::resultReady( const QString &collectionId, const Meta::GenreList &genres )
{
    Q_UNUSED( collectionId )
    foreach( Meta::GenrePtr genre, genres )
    {
        m_genres << genre->name();
    }
}

#include "EditFilterDialog.moc"

