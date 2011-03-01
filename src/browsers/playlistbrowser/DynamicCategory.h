/****************************************************************************************
 * Copyright (c) 2008 Daniel Jones <danielcjones@gmail.com>                             *
 * Copyright (c) 2009-2010 Leo Franchi <lfranchi@kde.org>                               *
 * Copyright (c) 2009 Mark Kretschmann <kretschmann@kde.org>                            *
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

#ifndef DYNAMICCATEGORY_H
#define DYNAMICCATEGORY_H

#include "browsers/BrowserCategory.h"

class QCheckBox;
class QPushButton;
class QToolButton;
class QStandardItemModel;
class QSpinBox;

namespace PlaylistBrowserNS {

    class DynamicView;
    class DynamicBiasDelegate;

    /**
    */
    class DynamicCategory : public BrowserCategory
    {
        Q_OBJECT
        public:
            DynamicCategory( QWidget* parent );
            ~DynamicCategory();

        private slots:
            void navigatorChanged();
            void selectionChanged();
            void save();
            void playlistCleared();
            void setUpcomingTracks( int );
            void setPreviousTracks( int );

        private:
            void saveOnExit();

            QCheckBox *m_onOffCheckbox;
            QPushButton *m_repopulateButton;

            QToolButton *m_addButton;
            QToolButton *m_cloneButton;
            QToolButton *m_editButton;
            QToolButton *m_deleteButton;
            DynamicView *m_tree;

            QSpinBox *m_previous, *m_upcoming;
    };

}

#endif
