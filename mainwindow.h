#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QSlider>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QMessageBox>
#include <QSpinBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QComboBox>
#include <QLineEdit>
#include <QColorDialog>
#include <QTimer>
#include <QDateTime>
#include <QImage>
#include <QPixmap>
#include <QColor>
#include <QDir>
#include <QCloseEvent>

#include <vector>
#include <string>
#include "HumanSeg.h"
#include "PreviewWidget.h"
#include "audiorecorder.h"
class BackgroundReplaceWindow : public QMainWindow
{
    Q_OBJECT

public:
    BackgroundReplaceWindow(QWidget *parent = nullptr);
    ~BackgroundReplaceWindow();

private slots:
    void updateFrame();
    void printTimeUp();
    void toggleTimerPrint();
    void updateConf();
    void toggleCamera();
    void toggleRecording();
    void onImageItemClicked(QListWidgetItem *item);
    void setCarouselInterval(int interval);
    void onTextChanged(const QString &text);
    void onPosXChanged(int value);
    void onPosYChanged(int value);
    void onFontSizeChanged(int value);
    void chooseColor();
    void imgMode();
    void videoMode();
    void addImages();
    void addVideo();
    void deleteSelectedImage();
    void clearAllImages();
    void startMix(QString inputPath);
private:
    void initUI();
    void closeEvent(QCloseEvent *event) override;
    int detectCamera();
    QString extractDirPathQt(const QString& fullPath);
    // Core components
    HumanSeg *segmentor;
    cv::VideoCapture *camera;
    QTimer *timer;
    QTimer *carouselTimer;

    // UI elements
    QWidget *centralWidget;
    QLabel *cameraLabel;
    QLabel *fpsLabel;
    QPushButton *btnCamera;
    QPushButton *btnTimer;
    QPushButton *recordBtn;
    QPushButton *btnAddImages;
    QPushButton *btnAddVideo;
    QPushButton *btnDeleteImage;
    QPushButton *btnClearImages;
    QSlider *confSlider;
    QSpinBox *intervalSpinBox;
    QLabel *intervalLabel;
    QGroupBox *listGroup;
    QListWidget *imageListWidget;
    ImagePreviewWidget *imagePreviewWidget;
    QRadioButton *radioImg;
    QRadioButton *radioVideo;
    QButtonGroup *bgTypeGroup;
    QComboBox *comboBox;
    QLineEdit *titleInputBox;
    QSpinBox *posXInput;
    QSpinBox *posYInput;
    QSpinBox *fsInput;
    audioRecorder *audioRec;

    // Data
    std::vector<std::string> imagePaths;
    int imgIndex;
    int carouselInterval;
    bool isRecording;
    cv::VideoWriter *videoWriter;
    std::string recordFilename;
    int cameraIndex;
    int cameraNumber;
    int camWidth;
    int camHeight;
    QString savePath;


    // FPS calculation - 使用滑动时间窗口
    std::vector<qint64> frameTimes; // 存储每帧的时间戳
    static constexpr int TIME_WINDOW_MS = 3000; // 3秒时间窗口
    float currentFPS;
};

#endif // MAINWINDOW_H
