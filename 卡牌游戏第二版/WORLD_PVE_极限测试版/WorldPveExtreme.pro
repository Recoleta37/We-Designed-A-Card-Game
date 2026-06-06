QT += widgets
CONFIG += c++17
TEMPLATE = app
TARGET = WorldPveExtreme

SOURCES += \
    main.cpp \
    MainWindow.cpp

HEADERS += \
    MainWindow.h

data.files = data
data.path = $$OUT_PWD
COPIES += data

assets.files = assets
assets.path = $$OUT_PWD
COPIES += assets
