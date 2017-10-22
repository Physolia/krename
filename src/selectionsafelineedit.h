/***************************************************************************
               selectionsafelinedit.h  -  description
                             -------------------
    begin                : Sat Jun 30 2007
    copyright            : (C) 2007 by Dominik Seichter
    email                : domseichter@web.de
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef SELECTIONSAFELINEEDIT_H
#define SELECTIONSAFELINEEDIT_H

#include <QLineEdit>

/** A QLineEdit that does not loose its selection
 *  on focusout events
 */
class SelectionSafeLineEdit : public QLineEdit {
public:
    /** Create a SeletionSafeLineEdit that keeps its selection
     *  even when the widget does not have the current keyboard focus.
     *
     *  @param parent parent widget
     */
    explicit SelectionSafeLineEdit( QWidget* parent = nullptr )
        : QLineEdit( parent )
    {

    }

protected:
    void focusOutEvent( QFocusEvent *  )
    {
        // ignore
    }

};

#endif // SELECTIONSAFELINEEDIT_H
