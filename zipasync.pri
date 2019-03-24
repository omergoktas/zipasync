##**************************************************************************
##
## Copyright (C) 2019 Ömer Göktaş
## Contact: omergoktas.com
##
## This file is part of the ZipAsync library.
##
## The ZipAsync is free software: you can redistribute it and/or
## modify it under the terms of the GNU Lesser General Public License
## version 3 as published by the Free Software Foundation.
##
## The ZipAsync is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
## GNU Lesser General Public License for more details.
##
## You should have received a copy of the GNU Lesser General Public
## License along with the ZipAsync. If not, see
## <https://www.gnu.org/licenses/>.
##
##**************************************************************************

CONFIG          += c++14

DEFINES         += ZIPASYNC_INCLUDE_STATIC \
                   MINIZ_NO_ZLIB_APIS \
                   MINIZ_NO_ZLIB_COMPATIBLE_NAMES

DEPENDPATH      += $$PWD
INCLUDEPATH     += $$PWD

SOURCES         += $$PWD/miniz.c \
                   $$PWD/zipasync.cpp

HEADERS         += $$PWD/miniz.h \
                   $$PWD/report.h \
                   $$PWD/zipasync.h \
                   $$PWD/zipasync_global.h

include($$PWD/async/async.pri)
