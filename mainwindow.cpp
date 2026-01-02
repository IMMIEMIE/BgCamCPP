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
    , videoWriter(nullptr)
    , cameraIndex(0)

    , currentFPS(0.0f)
{
    setWindowTitle("实时背景替换工具");
    setFixedSize(1600, 800);

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

    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Left control panel
    QWidget *leftWidget = new QWidget();
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
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

    // Camera start/stop button
    btnCamera = new QPushButton("启动摄像头");
    connect(btnCamera, &QPushButton::clicked, this, &BackgroundReplaceWindow::toggleCamera);
    controlLayout->addWidget(btnCamera);

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
    imagePreviewWidget = new ImagePreviewWidget();
    imagePreviewWidget->setMinimumHeight(200);
    previewLayout->addWidget(imagePreviewWidget);
    leftLayout->addWidget(previewGroup);
    leftLayout->addWidget(listGroup);
    leftLayout->addStretch();

    // Middle camera display area
    QWidget *camWidget = new QWidget();
    QVBoxLayout *camLayout = new QVBoxLayout(camWidget);
    camLayout->setContentsMargins(5, 5, 5, 5);

    QGroupBox *cameraGroup = new QGroupBox("摄像头预览（背景替换）");
    QVBoxLayout *cameraLayout = new QVBoxLayout(cameraGroup);

    cameraLabel = new QLabel();
    cameraLabel->setAlignment(Qt::AlignCenter);
    cameraLabel->setStyleSheet("border: 1px solid red; min-height: 550px;max-height: 550px;");
    cameraLayout->addWidget(cameraLabel);
    camLayout->addWidget(cameraGroup);

    // Right camera settings + recording settings
    QWidget *rightWidget = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
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
    titleInputBox = new QLineEdit();
    connect(titleInputBox, &QLineEdit::textChanged,
            this, &BackgroundReplaceWindow::onTextChanged);
    textLayout->addWidget(inputLabel);
    textLayout->addWidget(titleInputBox);

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

    QPushButton *colorBtn = new QPushButton("选择颜色");
    connect(colorBtn, &QPushButton::clicked, this, &BackgroundReplaceWindow::chooseColor);

    recordBtn = new QPushButton("开始录制");
    connect(recordBtn, &QPushButton::clicked, this, &BackgroundReplaceWindow::toggleRecording);

    camGroupLayout->addLayout(chooseLayout);
    camGroupLayout->addLayout(textLayout);
    camGroupLayout->addLayout(posXLayout);
    camGroupLayout->addLayout(posYLayout);
    camGroupLayout->addLayout(fsLayout);
    camGroupLayout->addWidget(colorBtn);
    camGroupLayout->addWidget(recordBtn);
    camGroupLayout->addStretch(1);
    rightLayout->addWidget(camGroup);
    rightLayout->addStretch();

    // Main layout assembly
    mainLayout->addWidget(leftWidget, 2);
    mainLayout->addWidget(camWidget, 6);
    mainLayout->addWidget(rightWidget, 2);
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
    if (!camera || !camera->isOpened()) {
        QMessageBox::warning(this, "警告", "请先启动摄像头再录制！");
        return;
    }

    if (!isRecording) {
        QString fileName = QFileDialog::getSaveFileName(
            this,
            "选择视频保存位置",
            QDir::currentPath() + "/output_video.mp4",
            "MP4 视频 (*.mp4);;AVI 视频 (*.avi);;所有文件 (*.*)"
            );

        if (fileName.isEmpty()) {
            QMessageBox::information(this, "提示", "已取消录制");
            return;
        }

        isRecording = true;
        recordBtn->setText("停止录制");

        // 设置视频录制参数
        double fps = 30.0; // 固定30fps确保稳定性
        int width = 960;
        int height = 560;

        int fourcc;
        if (fileName.toLower().endsWith(".avi")) {
            fourcc = cv::VideoWriter::fourcc('X', 'V', 'I', 'D');
        } else {
            fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
        }

        recordFilename = fileName.toStdString();
        videoWriter = new cv::VideoWriter(recordFilename, fourcc, fps, cv::Size(width, height));

        if (!videoWriter->isOpened()) {
            QMessageBox::critical(this, "错误", "视频写入器创建失败，请检查路径权限或编码格式");
            isRecording = false;
            recordBtn->setText("开始录制");
            delete videoWriter;
            videoWriter = nullptr;
            return;
        }

        QMessageBox::information(this, "录制开始", "视频录制已开始！");
    } else {
        isRecording = false;
        recordBtn->setText("开始录制");

        // 停止视频录制
        if (videoWriter) {
            videoWriter->release();
            delete videoWriter;
            videoWriter = nullptr;
        }

        QMessageBox::information(this, "录制完成", "录制完成！文件保存为：" + QString::fromStdString(recordFilename));
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
    } else {
        camera = new cv::VideoCapture(comboBox->currentIndex());
        if (!camera->isOpened()) {
            QMessageBox::critical(this, "错误", "无法打开摄像头！");
            return;
        }
        camera->set(cv::CAP_PROP_FRAME_WIDTH, 960);
        camera->set(cv::CAP_PROP_FRAME_HEIGHT, 560);
        timer->start(33);
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
                segmentor->setBackground(selectedPath.toStdString(), "image");
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

    if (isRecording && videoWriter) {
        cv::Mat writeFrame;
        cv::resize(outputFrame, writeFrame,
                   cv::Size(static_cast<int>(camera->get(cv::CAP_PROP_FRAME_WIDTH)),
                            static_cast<int>(camera->get(cv::CAP_PROP_FRAME_HEIGHT))));
        videoWriter->write(writeFrame);
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

int BackgroundReplaceWindow::detectCamera()
{
     qDebug() << "正在检测摄像头设备..." << '\n';
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
