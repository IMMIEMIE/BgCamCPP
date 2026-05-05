#ifndef PREVIEWWIDGET_H
#define PREVIEWWIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QPixmap>
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

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void initUI();
    void updateScaledPixmap();

    QString currentImagePath;
    QPixmap originalPixmap;
    QScrollArea *scrollArea;
    QLabel *imageLabel;
    QLabel *infoLabel;
};

#endif // PREVIEWWIDGET_H
