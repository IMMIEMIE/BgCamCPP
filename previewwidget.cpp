#include "PreviewWidget.h"
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPixmap>
#include <QFileInfo>
#include <QDir>
#include <QResizeEvent>
#include <QSize>

ImagePreviewWidget::ImagePreviewWidget(QWidget *parent)
    : QWidget(parent)
    , currentImagePath("")
{
    initUI();
}

void ImagePreviewWidget::initUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("border: 1px solid #cccccc;");
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    imageLabel = new QLabel();
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setStyleSheet("background-color: #f5f5f5;");
    imageLabel->setMinimumSize(1, 1);
    scrollArea->setWidget(imageLabel);
    layout->addWidget(scrollArea);

    infoLabel = new QLabel();
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setStyleSheet("font-size: 12px; color: #666666; padding: 5px;");
    layout->addWidget(infoLabel);
}

void ImagePreviewWidget::setImagePath(const QString &imagePath)
{
    if (!QFile::exists(imagePath)) {
        clear();
        infoLabel->setText("图片文件不存在！");
        return;
    }

    currentImagePath = imagePath;

    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        clear();
        infoLabel->setText("无法加载图片！");
        return;
    }

    originalPixmap = pixmap;
    updateScaledPixmap();

    QFileInfo fileInfo(imagePath);
    QString fileName = fileInfo.fileName();
    int width = pixmap.width();
    int height = pixmap.height();
    double fileSize = fileInfo.size() / 1024.0;  // KB

    infoLabel->setText(QString("%1 | %2×%3 | %4KB").arg(fileName).arg(width).arg(height).arg(fileSize, 0, 'f', 1));
}

void ImagePreviewWidget::clear()
{
    currentImagePath = "";
    originalPixmap = QPixmap();
    imageLabel->clear();
    infoLabel->clear();
}

void ImagePreviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateScaledPixmap();
}

void ImagePreviewWidget::updateScaledPixmap()
{
    if (originalPixmap.isNull() || !scrollArea) {
        return;
    }
    QSize target = scrollArea->viewport()->size();
    if (target.width() <= 0 || target.height() <= 0) {
        return;
    }
    // 按图片原宽高比例缩放到当前可视区域，保留 aspect ratio。
    QPixmap scaled = originalPixmap.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    imageLabel->setPixmap(scaled);
}
