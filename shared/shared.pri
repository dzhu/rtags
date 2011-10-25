INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD
SOURCES += $$PWD/GccArguments.cpp \
           $$PWD/Path.cpp \
           $$PWD/RTags.cpp \
           $$PWD/AtomicString.cpp 
HEADERS += $$PWD/GccArguments.h \
           $$PWD/Path.h \
           $$PWD/RTags.h \
           $$PWD/CursorKey.h \
           $$PWD/AtomicString.h
LIBS += -lmagic -lleveldb -lclang
include($$PWD/../3rdparty/leveldb.pri)
