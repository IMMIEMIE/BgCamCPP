QT       += core gui printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

FFMPEG_DIR = $${PWD}/ffmpeg-master-latest-win64-gpl-shared

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    ffmpegcamera.cpp \
    humanseg.cpp \
    main.cpp \
    mainwindow.cpp \
    previewwidget.cpp \
    puttext.cpp

HEADERS += \
    ffmpegcamera.h \
    humanseg.h \
    mainwindow.h \
    previewwidget.h \
    puttext.h

FORMS += \
    mainwindow.ui
INCLUDEPATH +=C:\Qt\opencv\forQt/install/include \
     C:\Qt\opencv\forQt/install/include/opencv2 \
     $${FFMPEG_DIR}/include \
     $${PWD}/onnxruntime-win-x64-1.23.2/include/

LIBS += C:\Qt\opencv\forQt/install/x64/mingw/lib/libopencv_*.a \
-L$${FFMPEG_DIR}/lib/ -lavdevice -lavformat -lavcodec -lavutil -lswscale \
-L$${PWD}/onnxruntime-win-x64-1.23.2/lib/ -lonnxruntime -lonnxruntime_providers_shared

# Windows平台下链接GDI32库
win32 {
    LIBS += -lgdi32
    DEFINES += UNICODE
    QMAKE_POST_LINK += $$quote(cmd /c if exist "$${FFMPEG_DIR}\\bin\\*.dll" copy /Y "$${FFMPEG_DIR}\\bin\\*.dll" "$(DESTDIR)" $$escape_expand(\\n\\t))
}
# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    ../docs.qrc \
    docs.qrc
