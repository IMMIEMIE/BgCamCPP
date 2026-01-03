QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    audiorecorder.cpp \
    humanseg.cpp \
    main.cpp \
    mainwindow.cpp \
    previewwidget.cpp \
    puttext.cpp

HEADERS += \
    audiorecorder.h \
    humanseg.h \
    mainwindow.h \
    previewwidget.h \
    puttext.h

FORMS += \
    mainwindow.ui
INCLUDEPATH +=C:\Qt\opencv\forQt/install/include \
     C:\Qt\opencv\forQt/install/include/opencv2 \
     $${PWD}/onnxruntime-win-x64-gpu-1.23.2/include/ \
     $${PWD}/onnxruntime-win-x64-1.23.2/include/

LIBS += C:\Qt\opencv\forQt/install/x64/mingw/lib/libopencv_*.a \
-L$${PWD}/onnxruntime-win-x64-gpu-1.23.2/lib/ -lonnxruntime -lonnxruntime_providers_shared \
-L$${PWD}/onnxruntime-win-x64-1.23.2/lib/ -lonnxruntime -lonnxruntime_providers_shared

# Windows平台下链接GDI32库
win32 {
    LIBS += -lgdi32
    DEFINES += UNICODE
}
# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
