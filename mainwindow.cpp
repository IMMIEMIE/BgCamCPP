#include "MainWindow.h"
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QProcess>
#include <QGraphicsDropShadowEffect>
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
    , currentSetupStep(-1)
    , currentBgPath("")
    , fgX(0)
    , fgY(0)
    , fgScale(1.0)
    , fgOpacity(1.0)
    , currentFPS(0.0f)
{
    setWindowTitle("实时背景替换工具");
    setFixedSize(1500, 800);

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
    rootStackedWidget = new QStackedWidget();
    setCentralWidget(rootStackedWidget);

    // ==========================================
    // 1. 创建标题界面 (Title Screen)
    // ==========================================
    titleScreenWidget = new QWidget();
    QVBoxLayout *titleLayout = new QVBoxLayout(titleScreenWidget);
    titleLayout->setAlignment(Qt::AlignCenter);
    titleLayout->setSpacing(40);

    // 1.1 添加 Logo 图片
    QLabel *logoLabel = new QLabel();
    QPixmap logoPixmap("e:\\Qt-projects\\BgCam\\BgCamCPP\\build\\Desktop_Qt_6_10_1_MinGW_64_bit-Release\\release\\icon.png");
    if (!logoPixmap.isNull()) {
        logoLabel->setPixmap(logoPixmap.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        logoLabel->setText("[Logo Not Found]");
    }
    logoLabel->setAlignment(Qt::AlignCenter);
    titleLayout->addWidget(logoLabel);

    // 1.2 添加学校名称
    QLabel *schoolLabel = new QLabel("厦门市康乐第二小学");
    schoolLabel->setAlignment(Qt::AlignCenter);
    QFont schoolFont("宋体", 24);
    schoolLabel->setFont(schoolFont);
    titleLayout->addWidget(schoolLabel);

    // 1.3 添加系统名称
    QLabel *systemLabel = new QLabel("正义智脑空间站宪法宣誓系统");
    systemLabel->setAlignment(Qt::AlignCenter);
    QFont systemFont("宋体", 36, QFont::Bold);
    systemLabel->setFont(systemFont);
    titleLayout->addWidget(systemLabel);

    // 1.4 添加“开始使用”按钮
    QPushButton *btnStart = new QPushButton("开始使用");
    btnStart->setFixedSize(250, 60);
    QFont btnFont("微软雅黑", 16);
    btnStart->setFont(btnFont);
    btnStart->setStyleSheet("QPushButton {"
                            "background-color: #E8F0E4;"
                            "border: 1px solid #333;"
                            "border-radius: 4px;"
                            "color: #333;"
                            "}"
                            "QPushButton:hover {"
                            "background-color: #D5E4CF;"
                            "}");
    connect(btnStart, &QPushButton::clicked, this, [this]() { rootStackedWidget->setCurrentWidget(bgTypeSelectionWidget); });
    
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(btnStart);
    btnLayout->addStretch();
    titleLayout->addLayout(btnLayout);

    rootStackedWidget->addWidget(titleScreenWidget);

    // ==========================================
    // 2. 背景类型选择界面
    // ==========================================
    bgTypeSelectionWidget = new QWidget();
    QVBoxLayout *typeLayout = new QVBoxLayout(bgTypeSelectionWidget);
    typeLayout->setContentsMargins(40, 40, 40, 40);
    typeLayout->addWidget(createWizardHeader());
    typeLayout->addSpacing(50);

    QVBoxLayout *radioLayout = new QVBoxLayout();
    radioLayout->setAlignment(Qt::AlignCenter);
    bgTypeGroup = new QButtonGroup(this);
    radioImg = new QRadioButton("图片背景");
    radioVideo = new QRadioButton("视频背景");
    radioImg->setFont(QFont("微软雅黑", 16));
    radioVideo->setFont(QFont("微软雅黑", 16));
    radioImg->setChecked(true);
    bgTypeGroup->addButton(radioImg, 1);
    bgTypeGroup->addButton(radioVideo, 2);
    radioLayout->addWidget(radioImg);
    radioLayout->addSpacing(20);
    radioLayout->addWidget(radioVideo);
    typeLayout->addLayout(radioLayout);
    typeLayout->addStretch();

    QHBoxLayout *typeBtnLayout = new QHBoxLayout();
    QPushButton *btnTypePrev = new QPushButton("上一步");
    QPushButton *btnTypeNext = new QPushButton("下一步");
    btnTypePrev->setFixedSize(150, 40);
    btnTypeNext->setFixedSize(150, 40);
    typeBtnLayout->addStretch();
    typeBtnLayout->addWidget(btnTypePrev);
    typeBtnLayout->addSpacing(50);
    typeBtnLayout->addWidget(btnTypeNext);
    typeBtnLayout->addStretch();
    typeLayout->addLayout(typeBtnLayout);
    rootStackedWidget->addWidget(bgTypeSelectionWidget);

    connect(btnTypePrev, &QPushButton::clicked, this, [this]() {
        rootStackedWidget->setCurrentWidget(titleScreenWidget);
    });
    connect(btnTypeNext, &QPushButton::clicked, this, [this]() {
        if (radioImg->isChecked()) {
            rootStackedWidget->setCurrentWidget(bgImageSetupWidget);
        } else {
            rootStackedWidget->setCurrentWidget(bgVideoSetupWidget);
        }
    });

    // ==========================================
    // 3-1. 图片背景设置界面
    // ==========================================
    bgImageSetupWidget = new QWidget();
    QVBoxLayout *imgSetupLayout = new QVBoxLayout(bgImageSetupWidget);
    imgSetupLayout->setContentsMargins(40, 40, 40, 40);
    imgSetupLayout->addWidget(createWizardHeader());
    imgSetupLayout->addSpacing(30);

    QHBoxLayout *imgContentLayout = new QHBoxLayout();
    // Left side: list and preview
    QVBoxLayout *imgLeftLayout = new QVBoxLayout();
    
    QHBoxLayout *listHLayout = new QHBoxLayout();
    listHLayout->addWidget(new QLabel("图片列表"));
    imageListWidget = new QListWidget();
    imageListWidget->setFixedHeight(100);
    imageListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(imageListWidget, &QListWidget::itemClicked, this, &BackgroundReplaceWindow::onImageItemClicked);
    listHLayout->addWidget(imageListWidget);
    imgLeftLayout->addLayout(listHLayout);

    QHBoxLayout *previewHLayout = new QHBoxLayout();
    previewHLayout->addWidget(new QLabel("图片预览"));
    imagePreviewWidget = new ImagePreviewWidget();
    imagePreviewWidget->setFixedSize(360, 250);
    previewHLayout->addWidget(imagePreviewWidget);
    previewHLayout->addStretch();
    imgLeftLayout->addLayout(previewHLayout);
    imgContentLayout->addLayout(imgLeftLayout);

    // Right side: buttons
    QVBoxLayout *imgRightLayout = new QVBoxLayout();
    btnAddImages = new QPushButton("选择背景图片");
    btnDeleteImage = new QPushButton("删除背景图片");
    btnClearImages = new QPushButton("清空背景图片");
    btnAddImages->setFixedSize(180, 40);
    btnDeleteImage->setFixedSize(180, 40);
    btnClearImages->setFixedSize(180, 40);
    
    connect(btnAddImages, &QPushButton::clicked, this, &BackgroundReplaceWindow::addImages);
    btnDeleteImage->setEnabled(false);
    connect(btnDeleteImage, &QPushButton::clicked, this, &BackgroundReplaceWindow::deleteSelectedImage);
    btnClearImages->setEnabled(false);
    connect(btnClearImages, &QPushButton::clicked, this, &BackgroundReplaceWindow::clearAllImages);
    
    imgRightLayout->addWidget(btnAddImages);
    imgRightLayout->addWidget(btnDeleteImage);
    imgRightLayout->addWidget(btnClearImages);
    imgRightLayout->addSpacing(20);
    
    QHBoxLayout *carouselLayout = new QHBoxLayout();
    chkEnableCarousel = new QCheckBox("启用轮播");
    intervalSpinBox = new QSpinBox();
    intervalSpinBox->setRange(1, 60);
    intervalSpinBox->setValue(5);
    connect(intervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &BackgroundReplaceWindow::setCarouselInterval);
    carouselLayout->addWidget(chkEnableCarousel);
    carouselLayout->addWidget(intervalSpinBox);
    carouselLayout->addWidget(new QLabel("秒"));
    carouselLayout->addStretch();
    imgRightLayout->addLayout(carouselLayout);
    imgRightLayout->addStretch();

    imgContentLayout->addLayout(imgRightLayout);
    imgSetupLayout->addLayout(imgContentLayout);
    imgSetupLayout->addStretch();

    QHBoxLayout *imgBtnLayout = new QHBoxLayout();
    QPushButton *btnImgPrev = new QPushButton("上一步");
    QPushButton *btnImgNext = new QPushButton("下一步");
    btnImgPrev->setFixedSize(150, 40);
    btnImgNext->setFixedSize(150, 40);
    imgBtnLayout->addStretch();
    imgBtnLayout->addWidget(btnImgPrev);
    imgBtnLayout->addSpacing(50);
    imgBtnLayout->addWidget(btnImgNext);
    imgBtnLayout->addStretch();
    imgSetupLayout->addLayout(imgBtnLayout);

    rootStackedWidget->addWidget(bgImageSetupWidget);

    connect(btnImgPrev, &QPushButton::clicked, this, [this]() {
        rootStackedWidget->setCurrentWidget(bgTypeSelectionWidget);
    });
    connect(btnImgNext, &QPushButton::clicked, this, [this]() {
        if (imagePaths.empty()) {
            QMessageBox::warning(this, "提示", "背景图片不可为空，请至少选择一张图片！");
            return;
        }
        showMainAppFromWizard();
    });

    // ==========================================
    // 3-2. 视频背景设置界面
    // ==========================================
    bgVideoSetupWidget = new QWidget();
    QVBoxLayout *vidSetupLayout = new QVBoxLayout(bgVideoSetupWidget);
    vidSetupLayout->setContentsMargins(40, 40, 40, 40);
    vidSetupLayout->addWidget(createWizardHeader());
    vidSetupLayout->addSpacing(30);

    QHBoxLayout *vidContentLayout = new QHBoxLayout();
    QVBoxLayout *vidLeftLayout = new QVBoxLayout();
    
    QHBoxLayout *nameLayout = new QHBoxLayout();
    nameLayout->addWidget(new QLabel("视频名称"));
    videoNameLabel = new QLabel();
    videoNameLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    videoNameLabel->setFixedHeight(30);
    nameLayout->addWidget(videoNameLabel);
    vidLeftLayout->addLayout(nameLayout);

    QHBoxLayout *vidPreviewLayout = new QHBoxLayout();
    vidPreviewLayout->addWidget(new QLabel("视频预览"));
    videoPreviewLabel = new QLabel();
    videoPreviewLabel->setFixedSize(360, 250);
    videoPreviewLabel->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    videoPreviewLabel->setAlignment(Qt::AlignCenter);
    vidPreviewLayout->addWidget(videoPreviewLabel);
    vidPreviewLayout->addStretch();
    vidLeftLayout->addLayout(vidPreviewLayout);
    vidContentLayout->addLayout(vidLeftLayout);

    QVBoxLayout *vidRightLayout = new QVBoxLayout();
    btnAddVideo = new QPushButton("选择背景视频");
    btnAddVideo->setFixedSize(180, 40);
    connect(btnAddVideo, &QPushButton::clicked, this, &BackgroundReplaceWindow::addVideo);
    vidRightLayout->addWidget(btnAddVideo);
    vidRightLayout->addStretch();

    vidContentLayout->addLayout(vidRightLayout);
    vidSetupLayout->addLayout(vidContentLayout);
    vidSetupLayout->addStretch();

    QHBoxLayout *vidBtnLayout = new QHBoxLayout();
    QPushButton *btnVidPrev = new QPushButton("上一步");
    QPushButton *btnVidNext = new QPushButton("下一步");
    btnVidPrev->setFixedSize(150, 40);
    btnVidNext->setFixedSize(150, 40);
    vidBtnLayout->addStretch();
    vidBtnLayout->addWidget(btnVidPrev);
    vidBtnLayout->addSpacing(50);
    vidBtnLayout->addWidget(btnVidNext);
    vidBtnLayout->addStretch();
    vidSetupLayout->addLayout(vidBtnLayout);

    rootStackedWidget->addWidget(bgVideoSetupWidget);

    connect(btnVidPrev, &QPushButton::clicked, this, [this]() {
        rootStackedWidget->setCurrentWidget(bgTypeSelectionWidget);
    });
    connect(btnVidNext, &QPushButton::clicked, this, [this]() {
        if (currentBgPath.isEmpty()) {
            QMessageBox::warning(this, "提示", "背景视频不可为空，请选择一个视频！");
            return;
        }
        showMainAppFromWizard();
    });

    // ==========================================
    // 2. 创建主应用界面 (Main App)
    // ==========================================
    centralWidget = new QWidget();
    
    cameraNumber = detectCamera();

    mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Middle camera display area
    camWidget = new QWidget();
    QVBoxLayout *camLayout = new QVBoxLayout(camWidget);
    camLayout->setContentsMargins(5, 5, 5, 5);

    cameraGroupBox = new QGroupBox("摄像头预览（F11全屏/退出全屏）");
    QVBoxLayout *cameraLayout = new QVBoxLayout(cameraGroupBox);
    cameraLayout->setSizeConstraint(QLayout::SetFixedSize);
    cameraGroupBox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);

    QWidget *previewContainer = new QWidget();
    previewContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    previewStackLayout = new QStackedLayout(previewContainer);
    previewStackLayout->setStackingMode(QStackedLayout::StackAll);
    previewStackLayout->setContentsMargins(0, 0, 0, 0);

    cameraLabel = new QLabel();
    cameraLabel->setAlignment(Qt::AlignCenter);
    cameraLabel->setMinimumSize(640, 480);
    cameraLabel->setMaximumSize(960, 540);
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
    camLayout->addWidget(cameraGroupBox, 0, Qt::AlignCenter);

    // Right control + camera settings + recording settings
    rightWidgetPanel = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidgetPanel);
    rightLayout->setContentsMargins(5, 5, 5, 5);
    rightLayout->setSpacing(8);

    setupStepLabel = new QLabel();
    setupStepLabel->setAlignment(Qt::AlignCenter);
    setupStepLabel->setStyleSheet("font-size: 28px; font-weight: bold; color: #fff; padding: 6px 0;");
    {
        auto *shadow = new QGraphicsDropShadowEffect(setupStepLabel);
        shadow->setBlurRadius(10);
        shadow->setOffset(2, 2);
        shadow->setColor(QColor(0, 0, 0, 220));
        setupStepLabel->setGraphicsEffect(shadow);
    }
    setupStepLabel->setVisible(false);
    rightLayout->addWidget(setupStepLabel);

    // Control panel group (moved from left to right)
    controlGroupBox = new QGroupBox("控制面板");
    QVBoxLayout *controlLayout = new QVBoxLayout(controlGroupBox);

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

    rightLayout->addWidget(controlGroupBox);

    QHBoxLayout *chooseLayout = new QHBoxLayout();
    QLabel *chooseLabel = new QLabel("选择摄像头：");
    chooseLayout->addWidget(chooseLabel);

    comboBox = new QComboBox();
    for (int i = 0; i < cameraNumber; ++i) {
        comboBox->addItem("相机" + QString::number(i + 1));
    }
    chooseLayout->addWidget(comboBox);
    chooseLayout->setStretchFactor(comboBox, 1);

    cameraControlGroupBox = new QGroupBox("摄像头设置");
    QVBoxLayout *cameraControlLayout = new QVBoxLayout(cameraControlGroupBox);

    btnCamera = new QPushButton("启动摄像头");
    connect(btnCamera, &QPushButton::clicked, this, &BackgroundReplaceWindow::toggleCamera);
    QLabel *cameraTipLabel = new QLabel("请先选择并启动摄像头，再进入下一步。");
    cameraTipLabel->setWordWrap(true);
    cameraTipLabel->setStyleSheet("color: #666;");
    cameraControlLayout->addLayout(chooseLayout);
    cameraControlLayout->addWidget(btnCamera);
    cameraControlLayout->addWidget(cameraTipLabel);
    rightLayout->addWidget(cameraControlGroupBox);

    textSettingsGroupBox = new QGroupBox("文字与录制设置");
    QVBoxLayout *camGroupLayout = new QVBoxLayout(textSettingsGroupBox);

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

    camGroupLayout->addLayout(textLayout);
    camGroupLayout->addLayout(fontLayout);
    camGroupLayout->addLayout(saveDirLayout);
    camGroupLayout->addLayout(posXLayout);
    camGroupLayout->addLayout(posYLayout);
    camGroupLayout->addLayout(fsLayout);
    camGroupLayout->addWidget(audioRec);
    camGroupLayout->addWidget(colorBtn);
    camGroupLayout->addWidget(recordBtn);
    camGroupLayout->addStretch(1);
    rightLayout->addWidget(textSettingsGroupBox);

    fgSettingsGroupBox = new QGroupBox("前景图片设置");
    QVBoxLayout *fgLayout = new QVBoxLayout(fgSettingsGroupBox);

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

    rightLayout->addWidget(fgSettingsGroupBox);

    QHBoxLayout *setupNavLayout = new QHBoxLayout();
    setupPrevBtn = new QPushButton("上一步");
    setupNextBtn = new QPushButton("下一步");
    setupPrevBtn->setVisible(false);
    setupNextBtn->setVisible(false);
    setupNavLayout->addWidget(setupPrevBtn);
    setupNavLayout->addWidget(setupNextBtn);
    rightLayout->addLayout(setupNavLayout);
    rightLayout->addStretch();

    connect(setupPrevBtn, &QPushButton::clicked, this, [this]() {
        if (currentSetupStep <= 0) {
            returnToBackgroundSetup();
            return;
        }
        showSetupStep(currentSetupStep - 1);
    });
    connect(setupNextBtn, &QPushButton::clicked, this, [this]() {
        if (currentSetupStep == 0) {
            if (!camera || !camera->isOpened()) {
                QMessageBox::warning(this, "提示", "请先选择并启动摄像头，再进入下一步。");
                return;
            }
            showSetupStep(1);
            return;
        }
        if (currentSetupStep == 1) {
            showSetupStep(2);
            return;
        }
        finishSetupFlow();
    });

    // Main layout assembly
    mainLayout->addWidget(camWidget, 8);
    mainLayout->addWidget(rightWidgetPanel, 2);

    rootStackedWidget->addWidget(centralWidget);
    rootStackedWidget->setCurrentWidget(titleScreenWidget);
}

void BackgroundReplaceWindow::showMainApp()
{
    rootStackedWidget->setCurrentWidget(centralWidget);
    finishSetupFlow();
}

QWidget* BackgroundReplaceWindow::createWizardHeader()
{
    QWidget *header = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(header);
    layout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    
    QLabel *logoLabel = new QLabel();
    QPixmap logoPixmap("e:\\Qt-projects\\BgCam\\BgCamCPP\\build\\Desktop_Qt_6_10_1_MinGW_64_bit-Release\\release\\icon.png");
    if (!logoPixmap.isNull()) {
        logoLabel->setPixmap(logoPixmap.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    
    QLabel *titleLabel = new QLabel("正义智脑空间站宪法宣誓系统");
    titleLabel->setFont(QFont("宋体", 28));
    
    layout->addWidget(logoLabel);
    layout->addSpacing(20);
    layout->addWidget(titleLabel);
    layout->addStretch();
    return header;
}

void BackgroundReplaceWindow::showMainAppFromWizard()
{
    rootStackedWidget->setCurrentWidget(centralWidget);
    applyBackgroundSelection();
    showSetupStep(0);
}

void BackgroundReplaceWindow::applyBackgroundSelection()
{
    if (radioImg->isChecked()) {
        if (chkEnableCarousel->isChecked()) {
            carouselInterval = intervalSpinBox->value();
            carouselTimer->start(carouselInterval * 1000);
        } else {
            carouselTimer->stop();
        }
        if (!imagePaths.empty()) {
            segmentor->setBackground(imagePaths[imgIndex], "image");
            currentBgPath = QString::fromStdString(imagePaths[imgIndex]);
        }
    } else if (radioVideo->isChecked()) {
        carouselTimer->stop();
        if (!currentBgPath.isEmpty()) {
            segmentor->setBackground(currentBgPath.toStdString(), "video");
        }
    }
}

void BackgroundReplaceWindow::showSetupStep(int step)
{
    currentSetupStep = step;
    rootStackedWidget->setCurrentWidget(centralWidget);

    const bool inSetupFlow = step >= 0 && step <= 2;
    if (!inSetupFlow) {
        finishSetupFlow();
        return;
    }

    cameraControlGroupBox->setVisible(step == 0);
    controlGroupBox->setVisible(step == 0);
    textSettingsGroupBox->setVisible(step == 1);
    fgSettingsGroupBox->setVisible(step == 2);
    setupStepLabel->setVisible(true);
    setupPrevBtn->setVisible(true);
    setupNextBtn->setVisible(true);
    setupPrevBtn->setText(step == 0 ? "返回背景设置" : "上一步");
    setupNextBtn->setText(step == 2 ? "完成" : "下一步");

    if (step == 0) {
        setupStepLabel->setText("步骤 1/3：选择并启动摄像头");
    } else if (step == 1) {
        setupStepLabel->setText("步骤 2/3：设置文字");
    } else {
        setupStepLabel->setText("步骤 3/3：设置前景图片");
    }
}

void BackgroundReplaceWindow::finishSetupFlow()
{
    currentSetupStep = -1;
    cameraControlGroupBox->show();
    controlGroupBox->show();
    textSettingsGroupBox->show();
    fgSettingsGroupBox->show();
    setupStepLabel->hide();
    setupPrevBtn->hide();
    setupNextBtn->hide();
}

void BackgroundReplaceWindow::returnToBackgroundSetup()
{
    currentSetupStep = -1;
    if (radioImg->isChecked()) {
        rootStackedWidget->setCurrentWidget(bgImageSetupWidget);
    } else {
        rootStackedWidget->setCurrentWidget(bgVideoSetupWidget);
    }
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
    if (!mainLayout || !camWidget || !rightWidgetPanel || !cameraGroupBox) {
        return;
    }

    if (!isPreviewFullScreen) {
        rightWidgetPanel->hide();

        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);
        qobject_cast<QVBoxLayout *>(camWidget->layout())->setContentsMargins(0, 0, 0, 0);
        qobject_cast<QVBoxLayout *>(cameraGroupBox->layout())->setContentsMargins(0, 0, 0, 0);
        qobject_cast<QVBoxLayout *>(cameraGroupBox->layout())->setSizeConstraint(QLayout::SetDefaultConstraint);

        cameraGroupBox->setTitle("");
        cameraGroupBox->setStyleSheet("QGroupBox { border: none; margin-top: 0px; }");
        cameraGroupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        if (QWidget *previewContainer = previewStackLayout->parentWidget()) {
            previewContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            previewContainer->setMinimumSize(0, 0);
            previewContainer->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
        cameraLabel->setMinimumSize(0, 0);
        cameraLabel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        showFullScreen();
        isPreviewFullScreen = true;
    } else {
        showNormal();
        rightWidgetPanel->show();

        mainLayout->setContentsMargins(10, 10, 10, 10);
        mainLayout->setSpacing(10);
        qobject_cast<QVBoxLayout *>(camWidget->layout())->setContentsMargins(5, 5, 5, 5);
        qobject_cast<QVBoxLayout *>(cameraGroupBox->layout())->setContentsMargins(9, 9, 9, 9);
        qobject_cast<QVBoxLayout *>(cameraGroupBox->layout())->setSizeConstraint(QLayout::SetFixedSize);

        cameraGroupBox->setTitle("摄像头预览（背景替换）");
        cameraGroupBox->setStyleSheet("");
        cameraGroupBox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        updateCameraPreviewSize(camWidth, camHeight);
        isPreviewFullScreen = false;
    }
}

void BackgroundReplaceWindow::updateCameraPreviewSize(int frameWidth, int frameHeight)
{
    const int safeWidth = frameWidth > 0 ? frameWidth : 640;
    const int safeHeight = frameHeight > 0 ? frameHeight : 480;
    const double aspectRatio = static_cast<double>(safeWidth) / static_cast<double>(safeHeight);

    int targetWidth = safeWidth;
    int targetHeight = safeHeight;

    const int maxPreviewWidth = 960;
    const int maxPreviewHeight = 620;

    if (targetWidth > maxPreviewWidth) {
        targetWidth = maxPreviewWidth;
        targetHeight = static_cast<int>(targetWidth / aspectRatio);
    }

    if (targetHeight > maxPreviewHeight) {
        targetHeight = maxPreviewHeight;
        targetWidth = static_cast<int>(targetHeight * aspectRatio);
    }

    targetWidth = std::max(targetWidth, 320);
    targetHeight = std::max(targetHeight, 240);

    if (QWidget *previewContainer = previewStackLayout->parentWidget()) {
        previewContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        previewContainer->setMinimumSize(targetWidth, targetHeight);
        previewContainer->setMaximumSize(targetWidth, targetHeight);
    }
    cameraLabel->setMinimumSize(targetWidth, targetHeight);
    cameraLabel->setMaximumSize(targetWidth, targetHeight);
    cameraGroupBox->adjustSize();
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
        this, "选择视频", QDir::currentPath(), "Video Files (*.mp4 *.avi *.mkv *.mov)");
    if (!videoPath.isEmpty()) {
        radioVideo->setChecked(true);
        currentBgPath = videoPath;
        
        QFileInfo fi(videoPath);
        videoNameLabel->setText(fi.fileName());
        
        cv::VideoCapture cap(videoPath.toStdString());
        if (cap.isOpened()) {
            cv::Mat frame;
            cap >> frame;
            if (!frame.empty()) {
                cv::Mat rgb;
                cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
                QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
                videoPreviewLabel->setPixmap(QPixmap::fromImage(img).scaled(videoPreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
            cap.release();
        }
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
        updateCameraPreviewSize(camWidth, camHeight);
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
