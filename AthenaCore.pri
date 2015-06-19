!contains(ATHENA_PRO, true): {
    INCLUDEPATH += $$PWD/include
    !contains(CONFIG, c++11):CONFIG += c++11
    mac:QMAKE_CXXFLAGS += -stdlib=libc++
}

include(extern/lzo/lzo.pri)
include(extern/zlib/zlib.pri)

ATHENA_CORE=true

SOURCES += \
    $$PWD/src/Athena/Utility.cpp \
    $$PWD/src/Athena/FileReader.cpp \
    $$PWD/src/Athena/FileWriter.cpp \
    $$PWD/src/Athena/MemoryReader.cpp \
    $$PWD/src/Athena/MemoryWriter.cpp \
    $$PWD/src/Athena/Global.cpp \
    $$PWD/src/Athena/Checksums.cpp \
    $$PWD/src/Athena/Compression.cpp \
    $$PWD/src/LZ77/LZLookupTable.cpp \
    $$PWD/src/LZ77/LZType10.cpp \
    $$PWD/src/LZ77/LZType11.cpp \
    $$PWD/src/LZ77/LZBase.cpp \
    $$PWD/src/Athena/FileInfo.cpp \
    $$PWD/src/Athena/Dir.cpp \
    $$PWD/src/gekko_support.c

win32:SOURCES += \
    $$PWD/src/win32_largefilewrapper.c

mac:SOURCES += \
    $$PWD/src/osx_largefilewrapper.c


HEADERS += \
    $$PWD/include/Athena/IStream.hpp \
    $$PWD/include/Athena/IStreamReader.hpp \
    $$PWD/include/Athena/IStreamWriter.hpp \
    $$PWD/include/Athena/Types.hpp \
    $$PWD/include/Athena/Utility.hpp \
    $$PWD/include/Athena/Global.hpp \
    $$PWD/include/Athena/Exception.hpp \
    $$PWD/include/Athena/FileNotFoundException.hpp \
    $$PWD/include/Athena/IOException.hpp \
    $$PWD/include/Athena/InvalidDataException.hpp \
    $$PWD/include/Athena/InvalidOperationException.hpp \
    $$PWD/include/Athena/FileReader.hpp \
    $$PWD/include/Athena/FileWriter.hpp \
    $$PWD/include/Athena/MemoryReader.hpp \
    $$PWD/include/Athena/MemoryWriter.hpp \
    $$PWD/include/Athena/NotImplementedException.hpp \
    $$PWD/include/Athena/Checksums.hpp \
    $$PWD/include/Athena/Compression.hpp \
    $$PWD/include/LZ77/LZBase.hpp \
    $$PWD/include/LZ77/LZLookupTable.hpp \
    $$PWD/include/LZ77/LZType10.hpp \
    $$PWD/include/LZ77/LZType11.hpp \
    $$PWD/include/utf8.h \
    $$PWD/include/utf8/checked.h \
    $$PWD/include/utf8/core.h \
    $$PWD/include/utf8/unchecked.h \
    $$PWD/include/Athena/FileInfo.hpp \
    $$PWD/include/Athena/Dir.hpp \
    $$PWD/include/gekko_support.h \
    $$PWD/include/Athena/DNA.hpp

win32:HEADERS += \
    $$PWD/include/win32_largefilewrapper.h

mac:HEADERS += \
    $$PWD/include/osx_largefilewrapper.h