#ifndef AUDIORECORDER_H
#define AUDIORECORDER_H
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QUrl>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QProcess>
QT_BEGIN_NAMESPACE
class QAudioBuffer;
class QMediaDevices;
QT_END_NAMESPACE
class audioRecorder: public QWidget
{
     Q_OBJECT
public:
    explicit audioRecorder(QWidget *parent = nullptr);
    void toggleRecord(QString audioPath);
    void saveAudio();
private:
    void init();
    void initUI();
    void updateDevices();
    void onStateChanged(QMediaRecorder::RecorderState state);
    QMediaFormat selectedMediaFormat() const;
    QMediaCaptureSession m_captureSession;
    QMediaRecorder *m_audioRecorder = nullptr;
    QMediaDevices *m_mediaDevices = nullptr;
    QComboBox *micChoose;
    QLabel *infoLabel;
    bool  m_updatingFormats = false;
};

#endif // AUDIORECORDER_H
