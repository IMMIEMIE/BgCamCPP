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
#include <QCheckBox>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QTimer>
#include <QDateTime>
#include <QImage>
#include <QPixmap>
#include <QColor>
#include <QDir>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QShortcut>
#include <QStringList>

#include <vector>
#include <string>
#include "ffmpegcamera.h"
#include "HumanSeg.h"
#include "PreviewWidget.h"
class BackgroundReplaceWindow : public QMainWindow
{
    Q_OBJECT

public:
    BackgroundReplaceWindow(QWidget *parent = nullptr);
    ~BackgroundReplaceWindow();

private slots:
    void updateFrame();
    void updateConf();
    void toggleCamera();
    void addImages();
    void addFgImage();
    void clearFgImage();
    void chooseSaveDirectory();
    void updateFgScale(int value);
    void updateFgOpacity(int value);
protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void initUI();
    void showMainApp();
    void showMainAppFromWizard();
    void applyBackgroundSelection();
    void showSetupStep(int step);
    void finishSetupFlow();
    void returnToBackgroundSetup();
    void goBackFromEscape();

    void updateFgPreview();
    void closeEvent(QCloseEvent *event) override;
    void detectCameras();
    void drawForeground(cv::Mat &frame);
    void toggleFullScreenPreview();
    void updateCameraPreviewSize(int frameWidth, int frameHeight);
    void moveForegroundBy(int dx, int dy);
    void adjustForegroundScale(int delta);
    void resetForegroundPosition();
    void updatePreviewStatusOverlay();
    void toggleFreezeFrame();
    void printFrozenFrame();
    
    // Core components
    HumanSeg *segmentor;
    FfmpegCamera *camera;
    QTimer *timer;

    // UI elements
    QStackedWidget *rootStackedWidget;
    QWidget *titleScreenWidget;
    QWidget *bgImageSetupWidget;

    QWidget *fgSetupWidget;

    QRadioButton *radioNoFg;
    QRadioButton *radioFg;
    QWidget *fgConfigPanel;
    QLabel *fgPreviewLabel;
    QSpinBox *fgScaleSpinBox;
    QSpinBox *fgPosXInput;
    QSpinBox *fgPosYInput;
    
    QWidget *centralWidget;
    QHBoxLayout *mainLayout;
    QWidget *camWidget;
    QWidget *rightWidgetPanel;
    QGroupBox *cameraGroupBox;
    QGroupBox *controlGroupBox;
    QGroupBox *cameraControlGroupBox;
    QGroupBox *peopleSettingsGroupBox;
    QGroupBox *fgSettingsGroupBox;
    QGroupBox *creationSettingsGroupBox;
    QStackedLayout *previewStackLayout;
    QLabel *cameraLabel;
    QLabel *previewStatusLabel;
    QLabel *fpsLabel;
    QLabel *setupStepLabel;
    QPushButton *btnCamera;
    QPushButton *freezeBtn;
    QPushButton *setupPrevBtn;
    QPushButton *setupNextBtn;
    QPushButton *btnAddImages;
    QSlider *confSlider;
    QGroupBox *listGroup;
    ImagePreviewWidget *imagePreviewWidget;
    QComboBox *comboBox;
    QComboBox *resolutionCombo;
    QLineEdit *saveDirInput;
    QPushButton *btnBrowseSaveDir;
    QPushButton *btnOpenSaveDir;

    QRadioButton *radioOnePerson;
    QRadioButton *radioTwoPerson;
    QButtonGroup *peopleGroup;

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

    bool isPreviewFullScreen;
    bool isFrameFrozen;
    int cameraIndex;
    QStringList cameraDeviceNames;
    int camWidth;
    int camHeight;
    int currentSetupStep;
    QString selectedImagePath;
    QString currentBgPath;
    QString frozenImagePath;
    cv::Mat latestOutputFrame;
    QImage frozenFrameImage;

    // 前景叠加缓存（drawForeground 性能优化）
    cv::Mat fgResizedCache;       // 已按 fgScale 缩放后的前景 BGR 图（去掉 alpha 通道）
    cv::Mat fgAlphaCache;         // 3 通道 CV_32F，已乘以 fgOpacity
    cv::Mat fgInvAlphaCache;      // 3 通道 CV_32F，1 - fgAlphaCache
    double fgCachedScale = -1.0;
    double fgCachedOpacity = -1.0;
    bool fgCacheDirty = true;

    // FPS calculation - 使用滑动时间窗口
    std::vector<qint64> frameTimes; // 存储每帧的时间戳
    static constexpr int TIME_WINDOW_MS = 3000; // 3秒时间窗口
    float currentFPS;
};

#endif // MAINWINDOW_H
