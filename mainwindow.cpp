#include "MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QProcess>
#include <iostream>

BackgroundReplaceWindow::BackgroundReplaceWindow(QWidget *parent)
    : QMainWindow(parent)
    , segmentor(new HumanSeg(0.5))
    , camera(nullptr)
    , timer(new QTimer(this))
    , carouselTimer(new QTimer(this))
    , imgIndex(0)
    , carouselInterval(5)
    , isRecording(false)
    , isPreviewFullScreen(false)
    , recordStartTime(0)
    , videoWriter(nullptr)
    , cameraIndex(0)
    , currentBgPath("")
    , currentFPS(0.0f)
    , fgX(0)
    , fgY(0)
    , fgScale(1.0)
    , fgOpacity(1.0)
{
    setWindowTitle("实时背景替换工具");
    setFixedSize(1800, 800);

    // Connect timer signals
    connect(timer, &QTimer::timeout, this, &BackgroundReplaceWindow::updateFrame);
    connect(carouselTimer, &QTimer::timeout, this, &BackgroundReplaceWindow::printTimeUp);

    initUI();
}

BackgroundReplaceWindow::~BackgroundReplaceWindow()
{
    // Clean up resources
    if (timer->isActive()) {
        timer->stop();
    }
    if (carouselTimer->isActive()) {
        carouselTimer->stop();
    }
    if (isRecording && videoWriter) {
        videoWriter->release();
        delete videoWriter;
        videoWriter = nullptr;
    }

    if (camera && camera->isOpened()) {
        camera->release();
        delete camera;
        camera = nullptr;
    }
    if (segmentor) {
        segmentor->release();
        delete segmentor;
        segmentor = nullptr;
    }
}

void BackgroundReplaceWindow::initUI()
{
    centralWidget = new QWidget();
    setCentralWidget(centralWidget);

    cameraNumber = detectCamera();

    mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Left control panel
    leftWidgetPanel = new QWidget();
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidgetPanel);
    leftLayout->setContentsMargins(5, 5, 5, 5);
    leftLayout->setSpacing(8);

    // Control panel group
    QGroupBox *controlGroup = new QGroupBox("控制面板");
    QVBoxLayout *controlLayout = new QVBoxLayout(controlGroup);

    // Background type selection
    QHBoxLayout *bgTypeLayout = new QHBoxLayout();
    bgTypeGroup = new QButtonGroup();
    radioImg = new QRadioButton("图片背景");
    radioVideo = new QRadioButton("视频背景");
    radioImg->setChecked(true);
    bgTypeGroup->addButton(radioImg, 1);
    bgTypeGroup->addButton(radioVideo, 2);
    bgTypeLayout->addWidget(radioImg);
    bgTypeLayout->addWidget(radioVideo);
    controlLayout->addLayout(bgTypeLayout);
    connect(radioImg, &QRadioButton::clicked, this, &BackgroundReplaceWindow::imgMode);
    connect(radioVideo, &QRadioButton::clicked, this, &BackgroundReplaceWindow::videoMode);

    // Image management buttons
    QVBoxLayout *imgManageLayout = new QVBoxLayout();

    btnAddImages = new QPushButton("添加背景图片");
    btnAddVideo = new QPushButton("添加背景视频");
    connect(btnAddImages, &QPushButton::clicked, this, &BackgroundReplaceWindow::addImages);
    connect(btnAddVideo, &QPushButton::clicked, this, &BackgroundReplaceWindow::addVideo);
    imgManageLayout->addWidget(btnAddImages);
    imgManageLayout->addWidget(btnAddVideo);
    btnAddVideo->hide();

    btnDeleteImage = new QPushButton("删除选中项");
    btnDeleteImage->setEnabled(false);
    connect(btnDeleteImage, &QPushButton::clicked, this, &BackgroundReplaceWindow::deleteSelectedImage);
    imgManageLayout->addWidget(btnDeleteImage);

    btnClearImages = new QPushButton("清空所有项");
    btnClearImages->setEnabled(false);
    connect(btnClearImages, &QPushButton::clicked, this, &BackgroundReplaceWindow::clearAllImages);
    imgManageLayout->addWidget(btnClearImages);

    controlLayout->addLayout(imgManageLayout);

    // Time interval spinbox
    QHBoxLayout *intervalLayout = new QHBoxLayout();
    intervalLabel = new QLabel("时间间隔（秒）：");
    intervalLayout->addWidget(intervalLabel);
    intervalSpinBox = new QSpinBox();
    intervalSpinBox->setRange(1, 60);
    intervalSpinBox->setValue(5);
    connect(intervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &BackgroundReplaceWindow::setCarouselInterval);
    intervalLayout->addWidget(intervalSpinBox);
    controlLayout->addLayout(intervalLayout);

    btnTimer = new QPushButton("启动轮播");
    connect(btnTimer, &QPushButton::clicked, this, &BackgroundReplaceWindow::toggleTimerPrint);
    controlLayout->addWidget(btnTimer);

    // Confidence slider
    QVBoxLayout *sliderLayout = new QVBoxLayout();
    sliderLayout->addWidget(new QLabel("分割置信度（越高越严格）"));
    confSlider = new QSlider(Qt::Horizontal);
    confSlider->setRange(1, 9);
    confSlider->setValue(5);
    connect(confSlider, &QSlider::valueChanged, this, &BackgroundReplaceWindow::updateConf);
    sliderLayout->addWidget(confSlider);
    controlLayout->addLayout(sliderLayout);

    // FPS display
    fpsLabel = new QLabel("FPS: 0.0");
    fpsLabel->setAlignment(Qt::AlignCenter);
    fpsLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #27ae60; padding: 8px; background-color: #d5f4e6; border-radius: 5px; border: 1px solid #27ae60;");
    controlLayout->addWidget(fpsLabel);

    controlLayout->addStretch();
    leftLayout->addWidget(controlGroup);

    // Image list
    listGroup = new QGroupBox("已导入图片列表");
    QVBoxLayout *listLayout = new QVBoxLayout(listGroup);

    imageListWidget = new QListWidget();
    imageListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(imageListWidget, &QListWidget::itemClicked,
            this, &BackgroundReplaceWindow::onImageItemClicked);
    imageListWidget->setFixedHeight(100);
    listLayout->addWidget(imageListWidget);

    // Image preview
    QGroupBox *previewGroup = new QGroupBox("图片预览");
    QVBoxLayout *previewLayout = new QVBoxLayout(previewGroup);
    previewGroup->setFixedSize(380, 300);
    imagePreviewWidget = new ImagePreviewWidget();
    imagePreviewWidget->setFixedSize(360, 250);
    previewLayout->addWidget(imagePreviewWidget, 0, Qt::AlignCenter);
    leftLayout->addWidget(previewGroup);
    leftLayout->addWidget(listGroup);
    leftLayout->addStretch();

    // Middle camera display area
    camWidget = new QWidget();
    QVBoxLayout *camLayout = new QVBoxLayout(camWidget);
    camLayout->setContentsMargins(5, 5, 5, 5);

    cameraGroupBox = new QGroupBox("摄像头预览（F11全屏/退出全屏）");
    QVBoxLayout *cameraLayout = new QVBoxLayout(cameraGroupBox);

    QWidget *previewContainer = new QWidget();
    previewStackLayout = new QStackedLayout(previewContainer);
    previewStackLayout->setStackingMode(QStackedLayout::StackAll);
    previewStackLayout->setContentsMargins(0, 0, 0, 0);

    cameraLabel = new QLabel();
    cameraLabel->setAlignment(Qt::AlignCenter);
    cameraLabel->setStyleSheet("min-height: 550px;max-height: 550px;");
    previewStackLayout->addWidget(cameraLabel);

    QWidget *overlayLayer = new QWidget();
    overlayLayer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QVBoxLayout *overlayRootLayout = new QVBoxLayout(overlayLayer);
    overlayRootLayout->setContentsMargins(12, 12, 12, 12);
    overlayRootLayout->addWidget(new QWidget(), 1);

    QHBoxLayout *overlayTopLayout = new QHBoxLayout();
    overlayTopLayout->addStretch();
    recordStatusLabel = new QLabel();
    recordStatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    recordStatusLabel->setVisible(false);
    recordStatusLabel->setStyleSheet(
        "QLabel {"
        "color: white;"
        "background-color: rgba(18, 18, 18, 180);"
        "border: 1px solid rgba(255, 255, 255, 35);"
        "border-radius: 12px;"
        "padding: 8px 14px;"
        "font-size: 14px;"
        "font-weight: 600;"
        "}"
    );
    overlayTopLayout->addWidget(recordStatusLabel, 0, Qt::AlignTop | Qt::AlignRight);
    overlayRootLayout->insertLayout(0, overlayTopLayout);
    previewStackLayout->addWidget(overlayLayer);

    cameraLayout->addWidget(previewContainer);
    camLayout->addWidget(cameraGroupBox);

    // Right camera settings + recording settings
    rightWidgetPanel = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidgetPanel);
    rightLayout->setContentsMargins(5, 5, 5, 5);
    rightLayout->setSpacing(8);

    QGroupBox *camGroup = new QGroupBox("画面设置");
    QVBoxLayout *camGroupLayout = new QVBoxLayout(camGroup);
    camGroupLayout->addLayout(bgTypeLayout);
    camGroupLayout->addStretch();
    rightLayout->addWidget(camGroup);

    QHBoxLayout *chooseLayout = new QHBoxLayout();
    QLabel *chooseLabel = new QLabel("选择摄像头：");
    chooseLayout->addWidget(chooseLabel);

    comboBox = new QComboBox();
    for (int i = 0; i < cameraNumber; ++i) {
        comboBox->addItem("相机" + QString::number(i + 1));
    }
    chooseLayout->addWidget(comboBox);
    chooseLayout->setStretchFactor(comboBox, 1);

    QHBoxLayout *textLayout = new QHBoxLayout();
    QLabel *inputLabel = new QLabel("宣誓标语：");
    titleInputBox = new QPlainTextEdit();
    titleInputBox->setMaximumHeight(80);
    connect(titleInputBox, &QPlainTextEdit::textChanged,
            this, [this]() {
                onTextChanged(titleInputBox->toPlainText());
            });
    textLayout->addWidget(inputLabel);
    textLayout->addWidget(titleInputBox);

    QHBoxLayout *fontLayout = new QHBoxLayout();
    QLabel *fontLabel = new QLabel("字体选择：");
    fontComboBox = new QFontComboBox();
    connect(fontComboBox, &QFontComboBox::currentFontChanged,
            this, [this](const QFont &font) {
                segmentor->setFontName(font.family().toStdString());
            });
    fontLayout->addWidget(fontLabel);
    fontLayout->addWidget(fontComboBox);

    QHBoxLayout *saveDirLayout = new QHBoxLayout();
    QLabel *saveDirLabel = new QLabel("保存目录：");
    saveDirInput = new QLineEdit();
    saveDirInput->setReadOnly(true);
    saveDirInput->setPlaceholderText("请选择视频保存目录");
    saveDirInput->setText(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    saveDirInput->setToolTip(saveDirInput->text());
    btnBrowseSaveDir = new QPushButton("选择...");
    connect(btnBrowseSaveDir, &QPushButton::clicked, this, &BackgroundReplaceWindow::chooseSaveDirectory);
    saveDirLayout->addWidget(saveDirLabel);
    saveDirLayout->addWidget(saveDirInput, 1);
    saveDirLayout->addWidget(btnBrowseSaveDir);

    QHBoxLayout *posXLayout = new QHBoxLayout();
    QLabel *posXLabel = new QLabel("左右位置：");
    posXInput = new QSpinBox();
    posXInput->setRange(1, 9999);
    posXInput->setValue(10);
    connect(posXInput, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &BackgroundReplaceWindow::onPosXChanged);
    posXLayout->addWidget(posXLabel);
    posXLayout->addWidget(posXInput);

    QHBoxLayout *posYLayout = new QHBoxLayout();
    QLabel *posYLabel = new QLabel("上下位置：");
    posYInput = new QSpinBox();
    posYInput->setRange(1, 9999);
    posYInput->setValue(10);
    connect(posYInput, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &BackgroundReplaceWindow::onPosYChanged);
    posYLayout->addWidget(posYLabel);
    posYLayout->addWidget(posYInput);

    QHBoxLayout *fsLayout = new QHBoxLayout();
    QLabel *fsLabel = new QLabel("字体大小：");
    fsInput = new QSpinBox();
    fsInput->setRange(15, 200);
    fsInput->setValue(40);
    connect(fsInput, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &BackgroundReplaceWindow::onFontSizeChanged);
    fsLayout->addWidget(fsLabel);
    fsLayout->addWidget(fsInput);

    QPushButton *colorBtn = new QPushButton("选择文字颜色");
    connect(colorBtn, &QPushButton::clicked, this, &BackgroundReplaceWindow::chooseColor);
    audioRec =new audioRecorder();
    recordBtn = new QPushButton("开始录制");
    connect(recordBtn, &QPushButton::clicked, this, &BackgroundReplaceWindow::toggleRecording);

    // Camera start/stop button
    btnCamera = new QPushButton("启动摄像头");
    connect(btnCamera, &QPushButton::clicked, this, &BackgroundReplaceWindow::toggleCamera);

    // Foreground image group
    QGroupBox *fgGroup = new QGroupBox("前景图片设置");
    QVBoxLayout *fgLayout = new QVBoxLayout(fgGroup);

    btnAddFgImage = new QPushButton("添加前景图片");
    connect(btnAddFgImage, &QPushButton::clicked, this, &BackgroundReplaceWindow::addFgImage);
    fgLayout->addWidget(btnAddFgImage);

    btnClearFgImage = new QPushButton("清空前景图片");
    btnClearFgImage->setEnabled(false);
    connect(btnClearFgImage, &QPushButton::clicked, this, &BackgroundReplaceWindow::clearFgImage);
    fgLayout->addWidget(btnClearFgImage);

    QHBoxLayout *fgScaleLayout = new QHBoxLayout();
    fgScaleLayout->addWidget(new QLabel("缩放:"));
    fgScaleSlider = new QSlider(Qt::Horizontal);
    fgScaleSlider->setRange(10, 300); // 0.1x to 3.0x
    fgScaleSlider->setValue(100);
    fgScaleSlider->setEnabled(false);
    connect(fgScaleSlider, &QSlider::valueChanged, this, &BackgroundReplaceWindow::updateFgScale);
    fgScaleLayout->addWidget(fgScaleSlider);
    fgLayout->addLayout(fgScaleLayout);

    QHBoxLayout *fgOpacityLayout = new QHBoxLayout();
    fgOpacityLayout->addWidget(new QLabel("透明度:"));
    fgOpacitySlider = new QSlider(Qt::Horizontal);
    fgOpacitySlider->setRange(0, 100); // 0.0 to 1.0
    fgOpacitySlider->setValue(100);
    fgOpacitySlider->setEnabled(false);
    connect(fgOpacitySlider, &QSlider::valueChanged, this, &BackgroundReplaceWindow::updateFgOpacity);
    fgOpacityLayout->addWidget(fgOpacitySlider);
    fgLayout->addLayout(fgOpacityLayout);

    auto bindForegroundShortcut = [this](const QKeySequence &sequence, const std::function<void()> &handler) {
        QShortcut *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };
    bindForegroundShortcut(QKeySequence(Qt::Key_Up), [this]() { moveForegroundBy(0, -10); });
    bindForegroundShortcut(QKeySequence(Qt::Key_Down), [this]() { moveForegroundBy(0, 10); });
    bindForegroundShortcut(QKeySequence(Qt::Key_Left), [this]() { moveForegroundBy(-10, 0); });
    bindForegroundShortcut(QKeySequence(Qt::Key_Right), [this]() { moveForegroundBy(10, 0); });
    bindForegroundShortcut(QKeySequence(Qt::Key_Plus), [this]() { adjustForegroundScale(5); });
    bindForegroundShortcut(QKeySequence(Qt::Key_Equal), [this]() { adjustForegroundScale(5); });
    bindForegroundShortcut(QKeySequence(Qt::Key_Minus), [this]() { adjustForegroundScale(-5); });
    bindForegroundShortcut(QKeySequence(Qt::Key_Space), [this]() { resetForegroundPosition(); });

    camGroupLayout->addLayout(chooseLayout);
    camGroupLayout->addLayout(textLayout);
    camGroupLayout->addLayout(fontLayout);
    camGroupLayout->addLayout(saveDirLayout);
    camGroupLayout->addLayout(posXLayout);
    camGroupLayout->addLayout(posYLayout);
    camGroupLayout->addLayout(fsLayout);
    camGroupLayout->addWidget(audioRec);
    camGroupLayout->addWidget(colorBtn);
    camGroupLayout->addWidget(recordBtn);
    camGroupLayout->addWidget(btnCamera);
    camGroupLayout->addStretch(1);
    rightLayout->addWidget(camGroup);
    rightLayout->addWidget(fgGroup);
    rightLayout->addStretch();

    // Main layout assembly
    mainLayout->addWidget(leftWidgetPanel, 2);
    mainLayout->addWidget(camWidget, 6);
    mainLayout->addWidget(rightWidgetPanel, 2);
}

void BackgroundReplaceWindow::addFgImage()
{
    QString fgPath = QFileDialog::getOpenFileName(
        this,
        "选择前景图片",
        QDir::currentPath(),
        "图片文件 (*.png *.jpg *.jpeg *.bmp)"
    );

    if (!fgPath.isEmpty()) {
        std::string pathStr = fgPath.toLocal8Bit().toStdString();
        fgImage = cv::imread(pathStr, cv::IMREAD_UNCHANGED); // Keep alpha channel if png
        if (fgImage.empty()) {
            // try to load with unicode support
            std::wstring wpath(pathStr.begin(), pathStr.end());
            FILE* fp = _wfopen(wpath.c_str(), L"rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long size = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                std::vector<char> buf(size);
                fread(buf.data(), 1, size, fp);
                fclose(fp);
                fgImage = cv::imdecode(cv::Mat(buf), cv::IMREAD_UNCHANGED);
            }
        }
        if (!fgImage.empty()) {
            QMessageBox::information(this, "成功", "前景图片加载成功，可用方向键移动，+/-缩放。");
            fgX = 0;
            fgY = 0;
            fgScale = 1.0;
            fgOpacity = 1.0;
            btnClearFgImage->setEnabled(true);
            fgScaleSlider->setValue(100);
            fgScaleSlider->setEnabled(true);
            fgOpacitySlider->setValue(100);
            fgOpacitySlider->setEnabled(true);
        } else {
            QMessageBox::critical(this, "错误", "加载前景图片失败！");
        }
    }
}

void BackgroundReplaceWindow::clearFgImage()
{
    fgImage.release();
    fgX = 0;
    fgY = 0;
    fgScale = 1.0;
    fgOpacity = 1.0;
    btnClearFgImage->setEnabled(false);
    fgScaleSlider->setValue(100);
    fgScaleSlider->setEnabled(false);
    fgOpacitySlider->setValue(100);
    fgOpacitySlider->setEnabled(false);
}

void BackgroundReplaceWindow::chooseSaveDirectory()
{
    QString startDir = saveDirInput->text().trimmed();
    if (startDir.isEmpty() || !QDir(startDir).exists()) {
        startDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    }

    QString dirPath = QFileDialog::getExistingDirectory(
        this,
        "选择视频保存目录",
        startDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (!dirPath.isEmpty()) {
        saveDirInput->setText(dirPath);
        saveDirInput->setToolTip(dirPath);
    }
}

void BackgroundReplaceWindow::updateFgScale(int value)
{
    fgScale = value / 100.0;
}

void BackgroundReplaceWindow::updateFgOpacity(int value)
{
    fgOpacity = value / 100.0;
}

void BackgroundReplaceWindow::moveForegroundBy(int dx, int dy)
{
    if (fgImage.empty()) {
        return;
    }
    fgX += dx;
    fgY += dy;
}

void BackgroundReplaceWindow::adjustForegroundScale(int delta)
{
    if (fgImage.empty() || !fgScaleSlider->isEnabled()) {
        return;
    }
    fgScaleSlider->setValue(qBound(fgScaleSlider->minimum(),
                                   fgScaleSlider->value() + delta,
                                   fgScaleSlider->maximum()));
}

void BackgroundReplaceWindow::resetForegroundPosition()
{
    if (fgImage.empty()) {
        return;
    }
    fgX = 0;
    fgY = 0;
}

void BackgroundReplaceWindow::updateRecordingStatusOverlay()
{
    if (!recordStatusLabel) {
        return;
    }

    if (!isRecording) {
        recordStatusLabel->clear();
        recordStatusLabel->hide();
        return;
    }

    qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - recordStartTime;
    int seconds = (elapsedMs / 1000) % 60;
    int minutes = (elapsedMs / (1000 * 60)) % 60;
    int hours = (elapsedMs / (1000 * 60 * 60));

    QString timeStr = QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));

    QString infoStr = QString("REC  %1\nFPS  %2")
        .arg(timeStr)
        .arg(currentFPS, 0, 'f', 1);
    recordStatusLabel->setText(infoStr);
    recordStatusLabel->show();
    recordStatusLabel->raise();
}

void BackgroundReplaceWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_F11:
            toggleFullScreenPreview();
            break;
        case Qt::Key_F12:
            toggleRecording();
            break;
        case Qt::Key_Escape:
            if (radioImg->isChecked() && imagePaths.size() > 0) {
                int currentRow = imageListWidget ? imageListWidget->currentRow() : -1;
                if (currentRow < 0) {
                    currentRow = imgIndex;
                }

                if (currentRow > 0) {
                    imgIndex = currentRow - 1;
                } else {
                    imgIndex = imagePaths.size() - 1;
                }
                imageListWidget->setCurrentRow(imgIndex);
                onImageItemClicked(imageListWidget->currentItem());
            }
            break;
    }
    QMainWindow::keyPressEvent(event);
}

void BackgroundReplaceWindow::toggleFullScreenPreview()
{
    if (!mainLayout || !camWidget || !leftWidgetPanel || !rightWidgetPanel || !cameraGroupBox) {
        return;
    }

    if (!isPreviewFullScreen) {
        leftWidgetPanel->hide();
        rightWidgetPanel->hide();

        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);
        qobject_cast<QVBoxLayout *>(camWidget->layout())->setContentsMargins(0, 0, 0, 0);
        qobject_cast<QVBoxLayout *>(cameraGroupBox->layout())->setContentsMargins(0, 0, 0, 0);

        cameraGroupBox->setTitle("");
        cameraGroupBox->setStyleSheet("QGroupBox { border: none; margin-top: 0px; }");
        cameraLabel->setStyleSheet("");
        showFullScreen();
        isPreviewFullScreen = true;
    } else {
        showNormal();
        leftWidgetPanel->show();
        rightWidgetPanel->show();

        mainLayout->setContentsMargins(10, 10, 10, 10);
        mainLayout->setSpacing(10);
        qobject_cast<QVBoxLayout *>(camWidget->layout())->setContentsMargins(5, 5, 5, 5);
        qobject_cast<QVBoxLayout *>(cameraGroupBox->layout())->setContentsMargins(9, 9, 9, 9);

        cameraGroupBox->setTitle("摄像头预览（背景替换）");
        cameraGroupBox->setStyleSheet("");
        cameraLabel->setStyleSheet("min-height: 550px;max-height: 550px;");
        isPreviewFullScreen = false;
    }
}

void BackgroundReplaceWindow::drawForeground(cv::Mat &frame)
{
    if (fgImage.empty()) return;

    cv::Mat resizedFg;
    cv::resize(fgImage, resizedFg, cv::Size(), fgScale, fgScale);

    int startX = fgX;
    int startY = fgY;

    for (int y = 0; y < resizedFg.rows; ++y) {
        int frameY = startY + y;
        if (frameY < 0 || frameY >= frame.rows) continue;

        for (int x = 0; x < resizedFg.cols; ++x) {
            int frameX = startX + x;
            if (frameX < 0 || frameX >= frame.cols) continue;

            if (resizedFg.channels() == 4) {
                cv::Vec4b fgPixel = resizedFg.at<cv::Vec4b>(y, x);
                double alpha = (fgPixel[3] / 255.0) * fgOpacity;
                
                if (alpha > 0) {
                    cv::Vec3b &bgPixel = frame.at<cv::Vec3b>(frameY, frameX);
                    for (int c = 0; c < 3; ++c) {
                        bgPixel[c] = static_cast<uchar>(fgPixel[c] * alpha + bgPixel[c] * (1.0 - alpha));
                    }
                }
            } else {
                cv::Vec3b fgPixel = resizedFg.at<cv::Vec3b>(y, x);
                double alpha = fgOpacity;
                cv::Vec3b &bgPixel = frame.at<cv::Vec3b>(frameY, frameX);
                for (int c = 0; c < 3; ++c) {
                    bgPixel[c] = static_cast<uchar>(fgPixel[c] * alpha + bgPixel[c] * (1.0 - alpha));
                }
            }
        }
    }
}

void BackgroundReplaceWindow::onTextChanged(const QString &text)
{
    //std::string stdStr=text.toStdString(); //UTF-8直接转换
    std::string gbk_str(text.toLocal8Bit().data());
    segmentor->setTitle(gbk_str);
}

void BackgroundReplaceWindow::onPosXChanged(int value)
{
    segmentor->setTitleX(value);
}

void BackgroundReplaceWindow::onPosYChanged(int value)
{
    segmentor->setTitleY(value);
}

void BackgroundReplaceWindow::onFontSizeChanged(int value)
{
    segmentor->setFontSize(value);
}

void BackgroundReplaceWindow::chooseColor()
{
    QColor initialColor(255, 255, 255);
    QColor color = QColorDialog::getColor(
        initialColor,
        this,
        "选择颜色",
        QColorDialog::ShowAlphaChannel
        );

    if (color.isValid()) {
        std::tuple<int, int, int> rgb = std::make_tuple(color.red(), color.green(), color.blue());
        segmentor->setRgb(rgb);
    }
}

void BackgroundReplaceWindow::toggleRecording()
{
    // ========== 检查摄像头是否已启动 ==========
    if (!camera || !camera->isOpened()) {
        QMessageBox::warning(this, "警告", "请先启动摄像头再录制！");
        return;
    }

    if (!isRecording) {
        QString dirPath = saveDirInput ? saveDirInput->text().trimmed() : QString();
        if (dirPath.isEmpty()) {
            QMessageBox::warning(this, "提示", "请先在右侧面板中选择视频保存目录。");
            return;
        }
        if (!QDir(dirPath).exists()) {
            QMessageBox::warning(this, "提示", "当前保存目录不存在，请重新选择。");
            return;
        }

        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
        QString videoPath = QDir(dirPath).filePath(timestamp + ".mp4");

        savePath = videoPath;
        if(videoPath.endsWith(".mp4", Qt::CaseInsensitive)){
            videoPath = extractDirPathQt(videoPath) + "tempVideo.mp4";
        }
        else{
            videoPath = extractDirPathQt(videoPath) + "tempVideo.avi";
        }
        // ========== 开始录制 ==========
        isRecording = true;
        recordStartTime = QDateTime::currentMSecsSinceEpoch();
        writtenFrames = 0;
        audioRec->toggleRecord(extractDirPathQt(videoPath));
        recordBtn->setText("停止录制");
        updateRecordingStatusOverlay();

        // ========== 获取摄像头画面的分辨率（强制为640x480，确保兼容性） ==========
        try {
            // 固定FPS为30.0，分辨率为640x480，确保兼容性
            const int width = camWidth;
            const int height = camHeight;
            const double fps = RECORD_FPS;
            
            // 根据用户选择的文件后缀自动选择编码格式
            QFileInfo fileInfo(videoPath);
            QString extension = fileInfo.suffix().toLower();
            
            int fourcc;
            if (extension == "avi") {
                fourcc = cv::VideoWriter::fourcc('X', 'V', 'I', 'D');  // AVI编码
            } else {
                fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');  // 默认MP4编码
            }

            // ========== 创建VideoWriter ==========
            videoWriter = new cv::VideoWriter(
                videoPath.toStdString(),
                fourcc,
                fps,
                cv::Size(width, height)
            );
            
            if (!videoWriter->isOpened()) {
                throw std::runtime_error("视频写入器创建失败，请检查路径权限或编码格式");
            }
            
            recordFilename = videoPath.toStdString();
            qDebug() << "开始录制：" << videoPath << '\n';
            qDebug() << "FPS：" << fps << "，分辨率：" << width << "x" << height << '\n';
            
        } catch (const std::exception &e) {
            QMessageBox::critical(this, "错误", QString("创建录制文件失败：%1").arg(e.what()));
            isRecording = false;
            recordBtn->setText("开始录制");
            updateRecordingStatusOverlay();
            if (videoWriter) {
                delete videoWriter;
                videoWriter = nullptr;
            }
            return;
        }
        
    } else {
        // ========== 停止录制 ==========
        isRecording = false;
        recordBtn->setText("开始录制");
        updateRecordingStatusOverlay();
        // 释放视频写入器
        if (videoWriter) {
            videoWriter->release();
            delete videoWriter;
            videoWriter = nullptr;
        }
        audioRec->saveAudio();
        startMix(savePath);
        qDebug() << "录制已停止，开始混合音视频：" << savePath << '\n';
    }
}

void BackgroundReplaceWindow::imgMode()
{
    btnAddImages->show();
    btnDeleteImage->show();
    btnClearImages->show();
    intervalSpinBox->show();
    btnTimer->show();
    intervalLabel->show();
    btnAddVideo->hide();
}

void BackgroundReplaceWindow::videoMode()
{
    btnAddImages->hide();
    btnDeleteImage->hide();
    btnClearImages->hide();
    intervalSpinBox->hide();
    btnTimer->hide();
    intervalLabel->hide();
    btnAddVideo->show();
    imagePreviewWidget->clear();
    imagePreviewWidget->setInfoText("当前为视频背景模式");
}

void BackgroundReplaceWindow::addImages()
{
    QStringList fileNames = QFileDialog::getOpenFileNames(
        this,
        "选择背景图片",
        QDir::currentPath(),
        "图片文件 (*.jpg *.jpeg *.png *.bmp *.gif *.tiff)"
        );

    if (fileNames.isEmpty()) {
        return;
    }

    int addedCount = 0;
    for (const QString &fileName : fileNames) {
        std::string filePath = fileName.toStdString();
        if (std::find(imagePaths.begin(), imagePaths.end(), filePath) == imagePaths.end()) {
            imagePaths.push_back(filePath);

            QListWidgetItem *item = new QListWidgetItem(QFileInfo(fileName).fileName());
            item->setData(Qt::UserRole, fileName);
            imageListWidget->addItem(item);
            addedCount++;
        }
    }

    btnDeleteImage->setEnabled(imageListWidget->count() > 0);
    btnClearImages->setEnabled(imageListWidget->count() > 0);

    if (addedCount > 0) {
        QMessageBox::information(this, "成功", QString("已添加 %1 张图片").arg(addedCount));
        imageListWidget->setCurrentRow(imageListWidget->count() - 1);
        onImageItemClicked(imageListWidget->currentItem());
    }
}

void BackgroundReplaceWindow::addVideo()
{
    QString videoPath = QFileDialog::getOpenFileName(
        this,
        "选择背景视频",
        QDir::currentPath(),
        "视频文件 (*.mp4 *.avi *.mov *.wmv)"
        );

    if (videoPath.isEmpty()) {
        QMessageBox::warning(this, "提示", "未选择任何视频文件！");
        return;
    }

    try {
        segmentor->setBackground(videoPath.toStdString(), "video");
        QMessageBox::information(this, "成功",
                                 QString("已设置视频背景：%1").arg(QFileInfo(videoPath).fileName()));
    } catch (const std::exception &e) {
        QMessageBox::critical(this, "错误", QString("视频加载失败：%1").arg(e.what()));
         qDebug() << "视频背景设置失败：" << e.what() << '\n';
    }
}

void BackgroundReplaceWindow::deleteSelectedImage()
{
    QListWidgetItem *currentItem = imageListWidget->currentItem();
    if (!currentItem) {
        QMessageBox::warning(this, "警告", "请先选择要删除的图片！");
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认删除",
        QString("确定要删除图片：%1 吗？").arg(currentItem->text()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
        );

    if (reply == QMessageBox::Yes) {
        std::string filePath = currentItem->data(Qt::UserRole).toString().toStdString();
        auto it = std::find(imagePaths.begin(), imagePaths.end(), filePath);
        if (it != imagePaths.end()) {
            imagePaths.erase(it);
        }

        int row = imageListWidget->row(currentItem);
        imageListWidget->takeItem(row);

        if (filePath == imagePreviewWidget->getCurrentImagePath().toStdString()) {
            imagePreviewWidget->clear();
        }

        btnDeleteImage->setEnabled(imageListWidget->count() > 0);
        btnClearImages->setEnabled(imageListWidget->count() > 0);

        QMessageBox::information(this, "成功", "图片已删除");
    }
}

void BackgroundReplaceWindow::clearAllImages()
{
    if (imageListWidget->count() == 0) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认清空",
        "确定要删除所有导入的图片吗？",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
        );

    if (reply == QMessageBox::Yes) {
        imagePaths.clear();
        imageListWidget->clear();
        imagePreviewWidget->clear();
        btnDeleteImage->setEnabled(false);
        btnClearImages->setEnabled(false);

        QMessageBox::information(this, "成功", "所有图片已清空");
    }
}



void BackgroundReplaceWindow::onImageItemClicked(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    imgIndex = imageListWidget->row(item);
    QString filePath = item->data(Qt::UserRole).toString();
    imagePreviewWidget->setImagePath(filePath);
    btnDeleteImage->setEnabled(true);
}

void BackgroundReplaceWindow::setCarouselInterval(int interval)
{
    carouselInterval = interval;
    if (carouselTimer->isActive()) {
        carouselTimer->stop();
        carouselTimer->start(interval * 1000);
         qDebug() << "轮播间隔已更新为：" << interval << "秒" << '\n';
    }
}

void BackgroundReplaceWindow::printTimeUp()
{
    if (imgIndex < static_cast<int>(imagePaths.size()) - 1) {
        imgIndex++;
    } else {
        imgIndex = 0;
    }
    imageListWidget->setCurrentRow(imgIndex);
    onImageItemClicked(imageListWidget->currentItem());
}

void BackgroundReplaceWindow::toggleTimerPrint()
{
    if (carouselTimer->isActive()) {
        carouselTimer->stop();
        btnTimer->setText("开始轮播");
         qDebug() << "定时轮播已停止" << '\n';
    } else {
        carouselTimer->start(carouselInterval * 1000);
        btnTimer->setText("停止轮播");
         qDebug() << "定时轮播已启动，间隔：" << carouselInterval << "秒" << '\n';
    }
}

void BackgroundReplaceWindow::updateConf()
{
    double conf = confSlider->value() / 10.0;
    segmentor->setConfThreshold(conf);
}

void BackgroundReplaceWindow::toggleCamera()
{
    if (timer->isActive()) {
        timer->stop();
        if (camera != nullptr) {
            camera->release();
            delete camera;
            camera = nullptr;
        }
        btnCamera->setText("启动摄像头");
        cameraLabel->clear();
        fpsLabel->setText("FPS: 0.0");
        frameTimes.clear();
        currentFPS = 0.0f;
        updateRecordingStatusOverlay();
    } else {
        camera = new cv::VideoCapture(comboBox->currentIndex());
        if (!camera->isOpened()) {
            QMessageBox::critical(this, "错误", "无法打开摄像头！");
            return;
        }
        camWidth = static_cast<int>(camera->get(cv::CAP_PROP_FRAME_WIDTH));
        camHeight = static_cast<int>(camera->get(cv::CAP_PROP_FRAME_HEIGHT));
        camera->set(cv::CAP_PROP_FRAME_WIDTH, camWidth);
        camera->set(cv::CAP_PROP_FRAME_HEIGHT, camHeight);
        timer->start(22);
        btnCamera->setText("停止摄像头");
        
        // Reset FPS calculation
        frameTimes.clear();
        currentFPS = 0.0f;
    }
}
QString BackgroundReplaceWindow::extractDirPathQt(const QString& fullPath) {
    if (fullPath.isEmpty()) {
        return "./";
    }
    // QFileInfo：解析文件路径的工具类
    QFileInfo fileInfo(fullPath);
    // absolutePath()：获取目录路径（无末尾分隔符），再用QDir补充分隔符
    QString dirPath = QDir(fileInfo.absolutePath()).absolutePath() + QDir::separator();
    return dirPath;
}
void BackgroundReplaceWindow::updateFrame()
{
    if (!camera || !camera->isOpened()) {
        return;
    }

    cv::Mat frame;
    bool ret = camera->read(frame);
    if (!ret) {
        return;
    }

    cv::flip(frame, frame, 1);
    cv::Mat outputFrame = frame;

    if (imageListWidget->currentItem()) {
        QString selectedPath = imageListWidget->currentItem()->data(Qt::UserRole).toString();
        try {
            if (radioImg->isChecked()) {
                if (currentBgPath != selectedPath || segmentor->getBgType() != "image") {
                    segmentor->setBackground(selectedPath.toStdString(), "image");
                    currentBgPath = selectedPath;
                }
                outputFrame = segmentor->segmentAndReplace(frame);
            } else if (radioVideo->isChecked()) {
                outputFrame = segmentor->segmentAndReplace(frame);
            }
        } catch (const std::exception &e) {
            outputFrame = frame;
             qDebug() << "背景替换失败：" << e.what() << '\n';
        }
    } else {
        if (radioVideo->isChecked() && segmentor->getBgType() == "video") {
            try {
                outputFrame = segmentor->segmentAndReplace(frame);
            } catch (const std::exception &e) {
                 qDebug() << "视频背景渲染失败：" << e.what() << '\n';
            }
        }
    }

    drawForeground(outputFrame);

    if (isRecording && videoWriter) {
        // 检查帧是否有效
        if (!outputFrame.empty() && outputFrame.cols > 0 && outputFrame.rows > 0) {
            cv::Mat writeFrame;
            
            // 将帧调整为640x480以匹配VideoWriter的配置
            cv::resize(outputFrame, writeFrame, cv::Size(640, 480));
            
            // 确保帧格式正确 (BGR格式)
            if (writeFrame.channels() == 3) {
                // 计算理论上应该写入多少帧 (根据录制时长和设定的FPS)
                qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - recordStartTime;
                int expectedFrames = static_cast<int>(elapsedMs * RECORD_FPS / 1000.0);
                
                // 补齐或跳过帧以保持音画同步
                while (writtenFrames < expectedFrames) {
                    videoWriter->write(writeFrame);
                    writtenFrames++;
                }
            } else {
                qDebug() << "警告：帧格式不正确，跳过此帧录制" << '\n';
            }
        } else {
            qDebug() << "警告：输出帧为空，跳过录制" << '\n';
        }

    }

    cv::Mat rgbFrame;
    cv::cvtColor(outputFrame, rgbFrame, cv::COLOR_BGR2RGB);

    QImage qtImage(rgbFrame.data,
                   rgbFrame.cols,
                   rgbFrame.rows,
                   rgbFrame.step,
                   QImage::Format_RGB888);

    QPixmap scaledPixmap = QPixmap::fromImage(qtImage).scaled(
        cameraLabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
        );

    cameraLabel->setPixmap(scaledPixmap);

    // FPS calculation - 使用滑动时间窗口
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    frameTimes.push_back(currentTime);

    // 移除3秒前的旧时间戳
    while (!frameTimes.empty() && (currentTime - frameTimes.front()) > TIME_WINDOW_MS) {
        frameTimes.erase(frameTimes.begin());
    }

    // 计算当前时间窗口内的FPS
    int frameCount = frameTimes.size();
    float timeWindowSec = TIME_WINDOW_MS / 1000.0f;
    currentFPS = static_cast<float>(frameCount) / timeWindowSec;

    // 更新显示
    fpsLabel->setText(QString("FPS: %1").arg(currentFPS, 0, 'f', 1));
    updateRecordingStatusOverlay();
}

void BackgroundReplaceWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "开始释放程序资源..." << '\n';

    if (timer->isActive()) {
        timer->stop();
         qDebug() << "摄像头定时器已停止" << '\n';
    }
    if (carouselTimer->isActive()) {
        carouselTimer->stop();
         qDebug() << "轮播定时器已停止" << '\n';
    }

    if (isRecording) {
        isRecording = false;
        updateRecordingStatusOverlay();
        if (videoWriter) {
            videoWriter->release();
            delete videoWriter;
            videoWriter = nullptr;
        }
        qDebug() << "录制资源已释放" << '\n';
    }

    try {
        if (camera && camera->isOpened()) {
            camera->release();
            delete camera;
            camera = nullptr;
        }
        qDebug() << "摄像头已释放" << '\n';
    } catch (const std::exception &e) {
        qDebug() << "摄像头释放警告：" << e.what() << '\n';
    }

    try {
        if (segmentor) {
            segmentor->release();
        }
    } catch (const std::exception &e) {
        qDebug() << "HumanSeg释放警告：" << e.what() << '\n';
    }

    event->accept();
}
void BackgroundReplaceWindow::startMix(QString inputPath)
{
    QString basePath=extractDirPathQt(inputPath);
    QString ffmpegPath = "ffmpeg-master-latest-win64-gpl-shared/bin/ffmpeg.exe";
    // 构造 FFmpeg 命令（替换视频音频，裁剪到最短时长）
    QStringList args;
    args << "-i" <<  (inputPath.back() == '4' ? basePath+"tempVideo.mp4" : basePath+"tempVideo.avi") // 输入视频
         << "-i" <<  basePath+"tempAudio.mp4a"     // 输入音频
         << "-map" << "0:v"              // 选择视频流
         << "-map" << "1:a"              // 选择音频流
         << "-c:v" << "copy"             // 视频拷贝
         << "-c:a" << "aac"              // 音频编码为 AAC
         << "-shortest"                  // 裁剪到最短时长
         << "-y"                         // 覆盖已有文件
         << inputPath;                  // 输出文件
    // 启动 FFmpeg 进程
    QProcess *ffmpegProcess = new QProcess();
    connect(ffmpegProcess, &QProcess::finished, this, [=](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitCode == 0) {
            qDebug()<<"混合成功！";
            QFile videoFile( (inputPath.back() == '4' ? basePath+"tempVideo.mp4" : basePath+"tempVideo.avi"));
            QFile audioFile(basePath+"tempAudio.mp4a");

            // 视频文件删除
            if (videoFile.exists()) {
                if (!videoFile.remove()) {
                    QMessageBox::warning(this, "提示",
                                         "视频文件删除失败");
                }
            }

            // 音频文件删除
            if (audioFile.exists()) {
                if (!audioFile.remove()) {
                    QMessageBox::warning(this, "提示",
                                         "音频文件删除失败：");
                }
            }

            QString fullPath = QFileInfo(inputPath).absoluteFilePath();
            QMessageBox::StandardButton reply = QMessageBox::question(
                this, "成功", 
                QString("录制成功！是否现在打开视频所在目录？\n%1").arg(fullPath),
                QMessageBox::Yes | QMessageBox::No
            );
            if (reply == QMessageBox::Yes) {
                QString dirPath = QFileInfo(fullPath).absolutePath();
                QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
            }
        } else {
            // 输出错误信息
            QString error = ffmpegProcess->readAllStandardError();
            qDebug()<<"----MiX-ERROR----\n"<<error;
        }
        ffmpegProcess->deleteLater();
    });

    // 启动进程
    ffmpegProcess->start(ffmpegPath, args);

    // 检查 FFmpeg 是否启动成功
    if (!ffmpegProcess->waitForStarted(3000)) {
        QMessageBox::critical(this, "错误", "无法启动 FFmpeg，请检查路径是否正确！");
        ffmpegProcess->deleteLater();
    }
}
int BackgroundReplaceWindow::detectCamera()
{
    int i = 0;
    while (true) {
        cv::VideoCapture cap(i, cv::CAP_DSHOW);
        if (cap.isOpened()) {
            cap.release();
            i++;
        } else {
            cap.release();
            break;
        }
    }
    return i;
}
