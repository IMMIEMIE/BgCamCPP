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
#include <QPlainTextEdit>
#include <QFontComboBox>
#include <QLineEdit>
#include <QStackedLayout>
#include <QColorDialog>
#include <QTimer>
#include <QDateTime>
#include <QImage>
#include <QPixmap>
#include <QColor>
#include <QDir>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QShortcut>

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
    void addFgImage();
    void clearFgImage();
    void chooseSaveDirectory();
    void updateFgScale(int value);
    void updateFgOpacity(int value);
    void deleteSelectedImage();
    void clearAllImages();
    void startMix(QString inputPath);
protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void initUI();
    void closeEvent(QCloseEvent *event) override;
    int detectCamera();
    QString extractDirPathQt(const QString& fullPath);
    void drawForeground(cv::Mat &frame);
    void toggleFullScreenPreview();
    void moveForegroundBy(int dx, int dy);
    void adjustForegroundScale(int delta);
    void resetForegroundPosition();
    void updateRecordingStatusOverlay();
    
    // Core components
    HumanSeg *segmentor;
    cv::VideoCapture *camera;
    QTimer *timer;
    QTimer *carouselTimer;

    // UI elements
    QWidget *centralWidget;
    QHBoxLayout *mainLayout;
    QWidget *leftWidgetPanel;
    QWidget *camWidget;
    QWidget *rightWidgetPanel;
    QGroupBox *cameraGroupBox;
    QStackedLayout *previewStackLayout;
    QLabel *cameraLabel;
    QLabel *recordStatusLabel;
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
    QPlainTextEdit *titleInputBox;
    QFontComboBox *fontComboBox;
    QLineEdit *saveDirInput;
    QPushButton *btnBrowseSaveDir;
    QSpinBox *posXInput;
    QSpinBox *posYInput;
    QSpinBox *fsInput;
    audioRecorder *audioRec;

    QPushButton *btnAddFgImage;
    QPushButton *btnClearFgImage;
    QSlider *fgScaleSlider;
    QSlider *fgOpacitySlider;
    
    // Data
    cv::Mat fgImage;
    int fgX;
    int fgY;
    double fgScale;
    double fgOpacity;

    std::vector<std::string> imagePaths;
    int imgIndex;
    int carouselInterval;
    bool isRecording;
    bool isPreviewFullScreen;
    qint64 recordStartTime;
    int writtenFrames;
    const double RECORD_FPS = 30.0;
    cv::VideoWriter *videoWriter;
    std::string recordFilename;
    int cameraIndex;
    int cameraNumber;
    int camWidth;
    int camHeight;
    QString savePath;
    QString currentBgPath;


    // FPS calculation - 使用滑动时间窗口
    std::vector<qint64> frameTimes; // 存储每帧的时间戳
    static constexpr int TIME_WINDOW_MS = 3000; // 3秒时间窗口
    float currentFPS;
};

#endif // MAINWINDOW_H
