/****************************************************************************
**
** Copyright (C) 2019 Ömer Göktaş
** Contact: omergoktas.com
**
** This file is part of the ZipAsync library.
**
** The ZipAsync is free software: you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public License
** version 3 as published by the Free Software Foundation.
**
** The ZipAsync is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public
** License along with the ZipAsync. If not, see
** <https://www.gnu.org/licenses/>.
**
****************************************************************************/

#ifndef ZIPASYNC_GLOBAL_H
#define ZIPASYNC_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(ZIPASYNC_LIBRARY) // takes precedence when combined with others
#  define ZIPASYNC_EXPORT Q_DECL_EXPORT
#elif defined(ZIPASYNC_INCLUDE_STATIC)
#  define ZIPASYNC_EXPORT
#else
#  define ZIPASYNC_EXPORT Q_DECL_IMPORT
#endif

#endif // ZIPASYNC_GLOBAL_H
