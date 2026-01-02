#ifndef PREVIEWWIDGET_H
#define PREVIEWWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSize>
#include <QString>

class ImagePreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ImagePreviewWidget(QWidget *parent = nullptr);

    void setImagePath(const QString &imagePath);
    void clear();
    QString getCurrentImagePath() const { return currentImagePath; }
    void setInfoText(const QString &text) { infoLabel->setText(text); }

private:
    void initUI();

    QString currentImagePath;
    QScrollArea *scrollArea;
    QLabel *imageLabel;
    QLabel *infoLabel;
};

#endif // PREVIEWWIDGET_H
