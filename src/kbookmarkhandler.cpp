/* This file is part of the KDE project
   Copyright (C) xxxx KFile Authors
   Copyright (C) 2002 Anders Lund <anders.lund@lund.tdcadsl.dk>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

// $Id$

#include "amarok.h"
#include "kbookmarkhandler.h"
#include <kbookmarkimporter.h>
#include <kbookmarkmenu.h>
#include <kdiroperator.h>
#include <kstandarddirs.h>

KBookmarkHandler::KBookmarkHandler( KDirOperator *parent, KMenu* rootmenu )
        : QObject( parent, "KBookmarkHandler" )
        , KBookmarkOwner()
{
    const QString file = Amarok::saveLocation() + "fileBrowserBookmarks.xml";

    // FIXME: which dbus object?
    KBookmarkManager *manager = KBookmarkManager::managerForFile( file, "", false );
    manager->setUpdate( true );

    new KBookmarkMenu( manager, this, rootmenu, 0 );
}

QString
KBookmarkHandler::currentURL() const
{
    return static_cast<KDirOperator*>(parent())->url().url();
}

void
KBookmarkHandler::openBookmarkURL( const QString &url )
{
    static_cast<KDirOperator*>(parent())->setUrl( KUrl(url), true );
}
