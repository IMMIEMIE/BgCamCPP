#include "MainWindow.h"
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QIcon>
#include <iostream>

BackgroundReplaceWindow::BackgroundReplaceWindow(QWidget *parent)
    : QMainWindow(parent)
    , segmentor(new HumanSeg(0.5))
    , camera(new FfmpegCamera())
    , timer(new QTimer(this))
    , carouselTimer(new QTimer(this))
    , imgIndex(0)
    , carouselInterval(5)
    , isPreviewFullScreen(false)
    , isFrameFrozen(false)
    , cameraIndex(0)
    , currentSetupStep(-1)
    , fgX(0)
    , fgY(0)
    , fgScale(1.0)
    , fgOpacity(1.0)
    , currentBgPath("")
    , currentFPS(0.0f)
{
    setWindowTitle("AI美学工坊美育创拍系统");
    setWindowIcon(QIcon(":/icon.png"));
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

    if (camera) {
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
    QPixmap logoPixmap(":/icon.png");
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
    QLabel *systemLabel = new QLabel("AI美学工坊美育创拍系统");
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
    
    detectCameras();

    mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Middle camera display area
    camWidget = new QWidget();
    QVBoxLayout *camLayout = new QVBoxLayout(camWidget);
    camLayout->setContentsMargins(5, 5, 5, 5);

    cameraGroupBox = new QGroupBox("摄像头预览（F10 全屏/退出全屏）");
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
    previewStatusLabel = new QLabel();
    previewStatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    previewStatusLabel->setVisible(false);
    previewStatusLabel->setStyleSheet(
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
    overlayTopLayout->addWidget(previewStatusLabel, 0, Qt::AlignTop | Qt::AlignRight);
    overlayRootLayout->insertLayout(0, overlayTopLayout);
    previewStackLayout->addWidget(overlayLayer);
    previewStackLayout->setCurrentWidget(overlayLayer);

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

    // Control panel group (moved to creation settings)
    controlGroupBox = new QGroupBox("控制面板");
    controlGroupBox->setVisible(false); // No longer used as a standalone group in wizard

    QHBoxLayout *chooseLayout = new QHBoxLayout();
    QLabel *chooseLabel = new QLabel("选择摄像头：");
    chooseLayout->addWidget(chooseLabel);

    comboBox = new QComboBox();
    for (const QString &deviceName : cameraDeviceNames) {
        comboBox->addItem(deviceName);
    }
    chooseLayout->addWidget(comboBox);
    chooseLayout->setStretchFactor(comboBox, 1);

    QHBoxLayout *resolutionLayout = new QHBoxLayout();
    QLabel *resolutionLabel = new QLabel("分辨率：");
    resolutionLayout->addWidget(resolutionLabel);

    resolutionCombo = new QComboBox();
    struct ResolutionPreset { const char *label; int w; int h; };
    const ResolutionPreset presets[] = {
        {"3.7MP 16:9 (2560x1440)", 2560, 1440},
        {"2.1MP 16:9 (1920x1080)", 1920, 1080},
        {"0.9MP 16:9 (1280x720)",  1280,  720},
        {"0.2MP 16:9 (640x360)",    640,  360},
        {"1.2MP 4:3 (1280x960)",   1280,  960},
        {"0.3MP 4:3 (640x480)",     640,  480},
    };
    for (const auto &p : presets) {
        resolutionCombo->addItem(QString::fromUtf8(p.label), QSize(p.w, p.h));
    }
    resolutionCombo->setCurrentIndex(2); // 默认 1280x720
    resolutionLayout->addWidget(resolutionCombo);
    resolutionLayout->setStretchFactor(resolutionCombo, 1);

    cameraControlGroupBox = new QGroupBox("摄像头设置");
    QVBoxLayout *cameraControlLayout = new QVBoxLayout(cameraControlGroupBox);

    btnCamera = new QPushButton("启动摄像头");
    connect(btnCamera, &QPushButton::clicked, this, &BackgroundReplaceWindow::toggleCamera);
    QLabel *cameraTipLabel = new QLabel("请先选择并启动摄像头，再进入下一步。\n注意：部分分辨率受摄像头硬件限制，可能会回退到默认值。");
    cameraTipLabel->setWordWrap(true);
    cameraTipLabel->setStyleSheet("color: #666;");
    cameraControlLayout->addLayout(chooseLayout);
    cameraControlLayout->addLayout(resolutionLayout);
    cameraControlLayout->addWidget(btnCamera);
    cameraControlLayout->addWidget(cameraTipLabel);
    rightLayout->addWidget(cameraControlGroupBox);

    textSettingsGroupBox = new QGroupBox("文字设置");
    QVBoxLayout *textSettingsLayout = new QVBoxLayout(textSettingsGroupBox);

    QHBoxLayout *textLayout = new QHBoxLayout();
    QLabel *inputLabel = new QLabel("文本：");
    titleInputBox = new QPlainTextEdit();
    titleInputBox->setMaximumHeight(80);
    connect(titleInputBox, &QPlainTextEdit::textChanged,
            this, [this]() {
                onTextChanged(titleInputBox->toPlainText());
            });
    textLayout->addWidget(inputLabel);
    textLayout->addWidget(titleInputBox);

    QHBoxLayout *fontLayout = new QHBoxLayout();
    QLabel *fontLabel = new QLabel("字体名称：");
    fontInputBox = new QLineEdit();
    fontInputBox->setPlaceholderText("请输入字体名称，例如：宋体、黑体");
    connect(fontInputBox, &QLineEdit::textChanged,
            this, [this](const QString &fontName) {
                if (!fontName.trimmed().isEmpty()) {
                    QByteArray localFontName = fontName.trimmed().toLocal8Bit();
                    segmentor->setFontName(std::string(localFontName.constData(), localFontName.size()));
                }
            });
    fontInputBox->setText("微软雅黑"); // 默认字体，并触发一次同步
    fontLayout->addWidget(fontLabel);
    fontLayout->addWidget(fontInputBox);

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

    textSettingsLayout->addLayout(textLayout);
    textSettingsLayout->addLayout(fontLayout);
    textSettingsLayout->addLayout(posXLayout);
    textSettingsLayout->addLayout(posYLayout);
    textSettingsLayout->addLayout(fsLayout);
    textSettingsLayout->addWidget(colorBtn);
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

    // Creation settings group (new step)
    creationSettingsGroupBox = new QGroupBox("创作设置");
    QVBoxLayout *creationLayout = new QVBoxLayout(creationSettingsGroupBox);

    // Confidence slider
    QVBoxLayout *confLayout = new QVBoxLayout();
    confLayout->addWidget(new QLabel("分割置信度（越高越严格）"));
    confSlider = new QSlider(Qt::Horizontal);
    confSlider->setRange(1, 9);
    confSlider->setValue(5);
    connect(confSlider, &QSlider::valueChanged, this, &BackgroundReplaceWindow::updateConf);
    confLayout->addWidget(confSlider);
    creationLayout->addLayout(confLayout);

    // FPS display
    fpsLabel = new QLabel("FPS: 0.0");
    fpsLabel->setAlignment(Qt::AlignCenter);
    fpsLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #27ae60; padding: 8px; background-color: #d5f4e6; border-radius: 5px; border: 1px solid #27ae60;");
    creationLayout->addWidget(fpsLabel);

    // Save directory selection
    QVBoxLayout *saveDirLayout = new QVBoxLayout();
    QHBoxLayout *saveDirInputRow = new QHBoxLayout();
    QHBoxLayout *saveDirButtonRow = new QHBoxLayout();
    QLabel *saveDirLabel = new QLabel("保存目录：");
    saveDirInput = new QLineEdit();
    saveDirInput->setReadOnly(true);
    saveDirInput->setPlaceholderText("请选择截图保存目录");
    saveDirInput->setText(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    saveDirInput->setToolTip(saveDirInput->text());
    btnBrowseSaveDir = new QPushButton("选择...");
    connect(btnBrowseSaveDir, &QPushButton::clicked, this, &BackgroundReplaceWindow::chooseSaveDirectory);
    
    btnOpenSaveDir = new QPushButton("打开该目录");
    connect(btnOpenSaveDir, &QPushButton::clicked, this, [this]() {
        QString path = saveDirInput->text();
        if (!path.isEmpty() && QDir(path).exists()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        } else {
            QMessageBox::warning(this, "提示", "目录不存在或未选择。");
        }
    });

    saveDirInputRow->addWidget(saveDirLabel);
    saveDirInputRow->addWidget(saveDirInput, 1);
    saveDirButtonRow->addStretch();
    saveDirButtonRow->addWidget(btnBrowseSaveDir);
    saveDirButtonRow->addWidget(btnOpenSaveDir);
    saveDirLayout->addLayout(saveDirInputRow);
    saveDirLayout->addLayout(saveDirButtonRow);
    creationLayout->addLayout(saveDirLayout);

    QLabel *hotkeyHintLabel = new QLabel("热键提示：F10 全屏切换，F12 冻结并截图/恢复，F11 打印冻结图片，ESC 切换上一张背景图");
    hotkeyHintLabel->setWordWrap(true);
    hotkeyHintLabel->setStyleSheet("color: #2c3e50; background: #ecf4ff; border: 1px solid #b5d3ff; border-radius: 6px; padding: 8px;");
    creationLayout->addWidget(hotkeyHintLabel);

    freezeBtn = new QPushButton("冻结并截图");
    freezeBtn->setMinimumHeight(50);
    freezeBtn->setStyleSheet("font-weight: bold; font-size: 18px; background-color: #2ecc71; color: white; border-radius: 8px;");
    connect(freezeBtn, &QPushButton::clicked, this, [this]() {
        if (currentSetupStep == 3 && !isPreviewFullScreen && !isFrameFrozen) {
            toggleFullScreenPreview();
            QApplication::processEvents();
        }
        toggleFreezeFrame();
    });
    creationLayout->addWidget(freezeBtn);

    rightLayout->addWidget(creationSettingsGroupBox);

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
        if (currentSetupStep == 2) {
            showSetupStep(3);
            return;
        }
        close();
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
    QPixmap logoPixmap(":/icon.png");
    if (!logoPixmap.isNull()) {
        logoLabel->setPixmap(logoPixmap.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    
    QLabel *titleLabel = new QLabel("AI美学工坊美育创拍系统");
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

    const bool inSetupFlow = step >= 0 && step <= 3;
    if (!inSetupFlow) {
        finishSetupFlow();
        return;
    }

    cameraControlGroupBox->setVisible(step == 0);
    textSettingsGroupBox->setVisible(step == 1);
    fgSettingsGroupBox->setVisible(step == 2);
    creationSettingsGroupBox->setVisible(step == 3);
    
    setupStepLabel->setVisible(true);
    setupPrevBtn->setVisible(true);
    setupNextBtn->setVisible(true);
    setupPrevBtn->setText(step == 0 ? "返回背景设置" : "上一步");
    setupNextBtn->setText(step == 3 ? "关闭" : "下一步");

    if (step == 0) {
        setupStepLabel->setText("步骤 1/4：选择并启动摄像头");
    } else if (step == 1) {
        setupStepLabel->setText("步骤 2/4：设置画面文本");
    } else if (step == 2) {
        setupStepLabel->setText("步骤 3/4：设置前景图片");
    } else {
        setupStepLabel->setText("步骤 4/4：准备截图");
        freezeBtn->setText(isFrameFrozen ? "恢复实时画面" : "冻结并截图");
    }
}

void BackgroundReplaceWindow::finishSetupFlow()
{
    currentSetupStep = -1;
    cameraControlGroupBox->show();
    textSettingsGroupBox->show();
    fgSettingsGroupBox->show();
    creationSettingsGroupBox->show();
    setupStepLabel->hide();
    setupPrevBtn->hide();
    setupNextBtn->hide();
    freezeBtn->setText(isFrameFrozen ? "恢复实时画面" : "冻结并截图");
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
            fgResizedCache.release();
            fgAlphaCache.release();
            fgInvAlphaCache.release();
            fgCacheDirty = true;
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
    fgResizedCache.release();
    fgAlphaCache.release();
    fgInvAlphaCache.release();
    fgCacheDirty = true;
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
        startDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }

    QString dirPath = QFileDialog::getExistingDirectory(
        this,
        "选择截图保存目录",
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

void BackgroundReplaceWindow::updatePreviewStatusOverlay()
{
    if (!previewStatusLabel) {
        return;
    }

    if (isFrameFrozen) {
        QString infoStr = "画面已冻结";
        if (!frozenImagePath.isEmpty()) {
            infoStr += "\nF11 打印  F12 恢复";
        } else {
            infoStr += "\n截图保存失败，F12 恢复";
        }
        previewStatusLabel->setText(infoStr);
        previewStatusLabel->show();
        previewStatusLabel->raise();
        return;
    }

    previewStatusLabel->clear();
    previewStatusLabel->hide();
}

void BackgroundReplaceWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_F10:
            if (currentSetupStep == 3) {
                toggleFullScreenPreview();
            }
            break;
        case Qt::Key_F11:
            if (currentSetupStep == 3) {
                printFrozenFrame();
            }
            break;
        case Qt::Key_F12:
            if (currentSetupStep == 3) {
                toggleFreezeFrame();
            }
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

void BackgroundReplaceWindow::toggleFreezeFrame()
{
    if (!camera || !camera->isOpened()) {
        QMessageBox::warning(this, "提示", "请先启动摄像头。");
        return;
    }

    if (isFrameFrozen) {
        isFrameFrozen = false;
        frozenFrameImage = QImage();
        frozenImagePath.clear();
        freezeBtn->setText("冻结并截图");
        updatePreviewStatusOverlay();
        return;
    }

    if (latestOutputFrame.empty()) {
        QMessageBox::warning(this, "提示", "当前还没有可用画面，请稍后再试。");
        return;
    }

    QImage qtImage(latestOutputFrame.data,
                   latestOutputFrame.cols,
                   latestOutputFrame.rows,
                   static_cast<int>(latestOutputFrame.step),
                   QImage::Format_BGR888);
    frozenFrameImage = qtImage.copy();
    if (frozenFrameImage.isNull()) {
        QMessageBox::warning(this, "提示", "冻结画面失败，请重试。");
        return;
    }

    QString snapshotDir = saveDirInput ? saveDirInput->text().trimmed() : QString();
    if (snapshotDir.isEmpty() || !QDir(snapshotDir).exists()) {
        snapshotDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    if (snapshotDir.isEmpty() || !QDir(snapshotDir).exists()) {
        snapshotDir = QDir::currentPath();
    }

    const QString fileName = QString("snapshot_%1.png")
                                 .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    const QString snapshotPath = QDir(snapshotDir).filePath(fileName);
    if (frozenFrameImage.save(snapshotPath, "PNG")) {
        frozenImagePath = snapshotPath;
    } else {
        frozenImagePath.clear();
    }

    cameraLabel->setPixmap(QPixmap::fromImage(frozenFrameImage).scaled(
        cameraLabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    ));
    isFrameFrozen = true;
    freezeBtn->setText("恢复实时画面");
    updatePreviewStatusOverlay();
}

void BackgroundReplaceWindow::printFrozenFrame()
{
    if (!isFrameFrozen || frozenFrameImage.isNull()) {
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(QPageLayout::Portrait);

    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QPainter painter(&printer);
    if (!painter.isActive()) {
        QMessageBox::warning(this, "提示", "打印启动失败。");
        return;
    }

    const QRectF pageRect = printer.pageRect(QPrinter::DevicePixel);
    QImage scaledImage = frozenFrameImage.scaled(
        pageRect.size().toSize(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    );
    const QPoint topLeft(static_cast<int>((pageRect.width() - scaledImage.width()) / 2.0),
                         static_cast<int>((pageRect.height() - scaledImage.height()) / 2.0));
    painter.drawImage(topLeft, scaledImage);
    painter.end();
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
        auto *camLayout = qobject_cast<QVBoxLayout *>(camWidget->layout());
        auto *cameraLayout = qobject_cast<QVBoxLayout *>(cameraGroupBox->layout());
        camLayout->setContentsMargins(0, 0, 0, 0);
        camLayout->setAlignment(cameraGroupBox, Qt::AlignCenter);
        cameraLayout->setContentsMargins(0, 0, 0, 0);
        cameraLayout->setSizeConstraint(QLayout::SetFixedSize);

        cameraGroupBox->setTitle("");
        cameraGroupBox->setStyleSheet("QGroupBox { border: none; margin-top: 0px; }");
        cameraGroupBox->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        if (QWidget *previewContainer = previewStackLayout->parentWidget()) {
            const QSize screenSize = QGuiApplication::primaryScreen()->availableGeometry().size();
            const int safeWidth = camWidth > 0 ? camWidth : 640;
            const int safeHeight = camHeight > 0 ? camHeight : 480;
            const double aspect = static_cast<double>(safeWidth) / static_cast<double>(safeHeight);

            int targetHeight = screenSize.height();
            int targetWidth = static_cast<int>(targetHeight * aspect);
            if (targetWidth > screenSize.width()) {
                targetWidth = screenSize.width();
                targetHeight = static_cast<int>(targetWidth / aspect);
            }

            previewContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            previewContainer->setMinimumSize(targetWidth, targetHeight);
            previewContainer->setMaximumSize(targetWidth, targetHeight);
            cameraLabel->setMinimumSize(targetWidth, targetHeight);
            cameraLabel->setMaximumSize(targetWidth, targetHeight);
        }
        showFullScreen();
        isPreviewFullScreen = true;
    } else {
        showNormal();
        rightWidgetPanel->show();

        mainLayout->setContentsMargins(10, 10, 10, 10);
        mainLayout->setSpacing(10);
        auto *camLayout = qobject_cast<QVBoxLayout *>(camWidget->layout());
        auto *cameraLayout = qobject_cast<QVBoxLayout *>(cameraGroupBox->layout());
        camLayout->setContentsMargins(5, 5, 5, 5);
        camLayout->setAlignment(cameraGroupBox, Qt::AlignCenter);
        cameraLayout->setContentsMargins(9, 9, 9, 9);
        cameraLayout->setSizeConstraint(QLayout::SetFixedSize);

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

    // 动态调整主窗口大小以适配预览分辨率
    if (!isPreviewFullScreen) {
        // 获取右侧面板的建议宽度，设置兜底值
        int sidebarWidth = rightWidgetPanel ? rightWidgetPanel->sizeHint().width() : 350;
        if (sidebarWidth < 300) sidebarWidth = 350;

        // 计算水平方向的总边距（包含布局间距）
        int horizontalMargins = 40; // 基础边距
        if (mainLayout) {
            horizontalMargins += mainLayout->contentsMargins().left() + mainLayout->contentsMargins().right() + mainLayout->spacing();
        }
        if (camWidget && camWidget->layout()) {
            horizontalMargins += camWidget->layout()->contentsMargins().left() + camWidget->layout()->contentsMargins().right();
        }

        // 计算总宽度：预览区宽度 + 右侧面板宽度 + 边距
        int totalWidth = targetWidth + sidebarWidth + horizontalMargins;
        
        // 计算总高度：取预览区高度和右侧面板高度的较大值，并加上垂直边距
        int sidebarHeight = rightWidgetPanel ? rightWidgetPanel->sizeHint().height() : 600;
        int verticalMargins = 60; // 基础垂直边距（包含标题栏等）
        if (mainLayout) {
            verticalMargins += mainLayout->contentsMargins().top() + mainLayout->contentsMargins().bottom();
        }
        int totalHeight = std::max(targetHeight + 80, sidebarHeight) + verticalMargins;

        // 获取屏幕可用尺寸，确保窗口不会超出屏幕
        QSize availableSize = QGuiApplication::primaryScreen()->availableGeometry().size();
        totalWidth = std::min(totalWidth, availableSize.width() - 20);
        totalHeight = std::min(totalHeight, availableSize.height() - 40);

        // 设置窗口为计算出的合适大小
        this->setFixedSize(totalWidth, totalHeight);
    }

    cameraLabel->updateGeometry();
    cameraGroupBox->updateGeometry();
    camWidget->updateGeometry();
}

void BackgroundReplaceWindow::drawForeground(cv::Mat &frame)
{
    if (fgImage.empty()) return;

    // 仅当源图/缩放/透明度变化时才重建缓存，正常播放时几乎零开销。
    const bool needRebuild = fgCacheDirty
        || fgResizedCache.empty()
        || std::abs(fgCachedScale - fgScale) > 1e-6
        || std::abs(fgCachedOpacity - fgOpacity) > 1e-6;

    if (needRebuild) {
        cv::Mat resized;
        cv::resize(fgImage, resized, cv::Size(), fgScale, fgScale, cv::INTER_LINEAR);

        if (resized.channels() == 4) {
            std::vector<cv::Mat> chs;
            cv::split(resized, chs);
            cv::Mat alpha8u = chs[3];
            cv::merge(std::vector<cv::Mat>{chs[0], chs[1], chs[2]}, fgResizedCache);

            cv::Mat alphaF;
            alpha8u.convertTo(alphaF, CV_32F, fgOpacity / 255.0);
            cv::merge(std::vector<cv::Mat>{alphaF, alphaF, alphaF}, fgAlphaCache);
            fgInvAlphaCache = cv::Scalar::all(1.0) - fgAlphaCache;
        } else {
            fgResizedCache = resized;
            fgAlphaCache.release();
            fgInvAlphaCache.release();
        }

        fgCachedScale = fgScale;
        fgCachedOpacity = fgOpacity;
        fgCacheDirty = false;
    }

    // 只对前景与画面相交的 ROI 区域做混合，画面外的像素直接跳过。
    const cv::Rect frameRect(0, 0, frame.cols, frame.rows);
    const cv::Rect fgRect(fgX, fgY, fgResizedCache.cols, fgResizedCache.rows);
    const cv::Rect roi = frameRect & fgRect;
    if (roi.width <= 0 || roi.height <= 0) return;

    const cv::Rect fgRoi(roi.x - fgX, roi.y - fgY, roi.width, roi.height);
    cv::Mat fgPart = fgResizedCache(fgRoi);
    cv::Mat framePart = frame(roi);

    if (!fgAlphaCache.empty()) {
        // 带 alpha 通道：out = fg * alpha + bg * (1 - alpha)
        // 改用 OpenCV 矢量化运算（SIMD），代替原先的逐像素 at<> 循环。
        cv::Mat alphaPart = fgAlphaCache(fgRoi);
        cv::Mat invAlphaPart = fgInvAlphaCache(fgRoi);

        cv::Mat fgF, frameF;
        fgPart.convertTo(fgF, CV_32FC3);
        framePart.convertTo(frameF, CV_32FC3);
        cv::Mat blended = fgF.mul(alphaPart) + frameF.mul(invAlphaPart);
        blended.convertTo(framePart, CV_8UC3);
    } else {
        // 无 alpha 通道：整体透明度做线性混合，addWeighted 一步到位。
        cv::addWeighted(fgPart, fgOpacity, framePart, 1.0 - fgOpacity, 0.0, framePart);
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
        isFrameFrozen = false;
        frozenFrameImage = QImage();
        frozenImagePath.clear();
        latestOutputFrame.release();
        if (camera) {
            camera->release();
        }
        btnCamera->setText("启动摄像头");
        cameraLabel->clear();
        fpsLabel->setText("FPS: 0.0");
        frameTimes.clear();
        currentFPS = 0.0f;
        freezeBtn->setText("冻结并截图");
        comboBox->setEnabled(true);
        resolutionCombo->setEnabled(true);
        updatePreviewStatusOverlay();
    } else {
        if (cameraDeviceNames.isEmpty() || comboBox->currentIndex() < 0) {
            QMessageBox::critical(this, "错误", "未检测到可用摄像头！");
            return;
        }

        if (!camera) {
            camera = new FfmpegCamera();
        }

        const QString deviceName = cameraDeviceNames.value(comboBox->currentIndex());
        const QSize requested = resolutionCombo->currentData().toSize();

        // 在阻塞 open 之前刷新一下 UI，让"正在启动..."能立刻显示出来。
        // Windows 下打开 USB 摄像头单次就要 1–3 秒，没有反馈的话像卡死。
        btnCamera->setEnabled(false);
        btnCamera->setText("正在启动...");
        comboBox->setEnabled(false);
        resolutionCombo->setEnabled(false);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QApplication::processEvents();

        QString openError;
        const bool opened = camera->open(
            deviceName,
            requested.isValid() ? requested.width() : 1280,
            requested.isValid() ? requested.height() : 720,
            30,
            &openError);

        if (!opened) {
            QApplication::restoreOverrideCursor();
            btnCamera->setEnabled(true);
            btnCamera->setText("启动摄像头");
            comboBox->setEnabled(true);
            resolutionCombo->setEnabled(true);
            QMessageBox::critical(this, "错误", openError.isEmpty() ? "无法打开摄像头！" : openError);
            return;
        }
        QApplication::restoreOverrideCursor();

        camWidth = camera->width();
        camHeight = camera->height();

        if (requested.isValid() &&
            (camWidth != requested.width() || camHeight != requested.height())) {
            QMessageBox::information(
                this, "提示",
                QString("摄像头不支持 %1x%2，已回退为 %3x%4")
                    .arg(requested.width()).arg(requested.height())
                    .arg(camWidth).arg(camHeight));
        }

        updateCameraPreviewSize(camWidth, camHeight);
        // 用 0ms 让定时器在事件循环空闲时立即重排：实际帧率由相机读取+推理决定，不再被 22ms 硬上限卡住
        timer->start(0);
        btnCamera->setEnabled(true);
        btnCamera->setText("停止摄像头");

        // Reset FPS calculation
        frameTimes.clear();
        currentFPS = 0.0f;
    }
}

void BackgroundReplaceWindow::updateFrame()
{
    if (!camera || !camera->isOpened()) {
        return;
    }

    if (isFrameFrozen) {
        if (!frozenFrameImage.isNull()) {
            cameraLabel->setPixmap(QPixmap::fromImage(frozenFrameImage).scaled(
                cameraLabel->size(),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            ));
        }
        updatePreviewStatusOverlay();
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

    // 分割路径返回的是新分配的 Mat，可以浅拷贝；
    // 无分割路径下 outputFrame 还指向相机内部缓冲，必须 clone，否则下一次 read 会改写其数据
    if (outputFrame.data == frame.data) {
        latestOutputFrame = outputFrame.clone();
    } else {
        latestOutputFrame = outputFrame;
    }

    // 直接用 BGR888 喂给 QImage，省掉一次整图 cvtColor
    QImage qtImage(outputFrame.data,
                   outputFrame.cols,
                   outputFrame.rows,
                   static_cast<int>(outputFrame.step),
                   QImage::Format_BGR888);

    // 实时预览用 FastTransformation：肉眼几乎无差，但缩放成本远低于 SmoothTransformation
    QPixmap scaledPixmap = QPixmap::fromImage(qtImage).scaled(
        cameraLabel->size(),
        Qt::KeepAspectRatio,
        Qt::FastTransformation
        );

    cameraLabel->setPixmap(scaledPixmap);

    // FPS：3 秒滑动时间窗口
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    frameTimes.push_back(currentTime);
    while (!frameTimes.empty() && (currentTime - frameTimes.front()) > TIME_WINDOW_MS) {
        frameTimes.erase(frameTimes.begin());
    }
    currentFPS = static_cast<float>(frameTimes.size()) / (TIME_WINDOW_MS / 1000.0f);

    fpsLabel->setText(QString("FPS: %1").arg(currentFPS, 0, 'f', 1));
    updatePreviewStatusOverlay();
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

void BackgroundReplaceWindow::detectCameras()
{
    QString errorMessage;
    cameraDeviceNames = FfmpegCamera::listVideoDevices(&errorMessage);
    if (cameraDeviceNames.isEmpty() && !errorMessage.isEmpty()) {
        qDebug() << errorMessage;
    }
}
