#include "audiorecorder.h"
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QAudioBuffer>
#include <QAudioDevice>
#include <QAudioInput>
#include <QDir>
#include <QFileDialog>
#include <QImageCapture>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QMimeType>
#include <QStandardPaths>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QApplication>
#if QT_CONFIG(permissions)
#include <QPermission>
#endif
audioRecorder::audioRecorder(QWidget *parent) : QWidget(parent)
{
    init();
    initUI();
}
void audioRecorder::init(){
#if QT_CONFIG(permissions)
    QMicrophonePermission microphonePermission;
    switch (qApp->checkPermission(microphonePermission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(microphonePermission, this, &audioRecorder::init);
        return;
    case Qt::PermissionStatus::Denied:
        qWarning("Microphone permission is not granted!");
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
#endif
    micChoose = new QComboBox();
    infoLabel = new QLabel("选择麦克风：");
    m_audioRecorder = new QMediaRecorder(this);
    m_captureSession.setRecorder(m_audioRecorder);
    m_captureSession.setAudioInput(new QAudioInput(this));
    // audio devices
    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this,
            &audioRecorder::updateDevices);
    updateDevices();
}
static QVariant boxValue(const QComboBox *box)
{
    int idx = box->currentIndex();
    if (idx == -1)
        return QVariant();

    return box->itemData(idx);
}
void audioRecorder::initUI()
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(infoLabel);
    layout->addWidget(micChoose);
}
void audioRecorder::toggleRecord(QString audioPath)
{
    if (!m_audioRecorder) {
        return;
    }
    if (m_audioRecorder->recorderState() == QMediaRecorder::StoppedState) {
        m_audioRecorder->setOutputLocation(QUrl::fromLocalFile(audioPath));
        m_captureSession.audioInput()->setDevice(
            boxValue(micChoose).value<QAudioDevice>());
        m_audioRecorder->setMediaFormat(selectedMediaFormat());
        m_audioRecorder->setAudioSampleRate(64000);
        m_audioRecorder->setAudioBitRate(64000);
        m_audioRecorder->setAudioChannelCount(-1);
        m_audioRecorder->setQuality(QMediaRecorder::Quality(QImageCapture::NormalQuality));
        m_audioRecorder->setEncodingMode(QMediaRecorder::ConstantQualityEncoding);
        m_audioRecorder->record();
        qDebug()<<"m_audioRecorder->error():"<<m_audioRecorder->error();
    }
}
void audioRecorder::saveAudio(){
    if(m_audioRecorder->recorderState() == QMediaRecorder::StoppedState){
        qDebug()<<"又没录上！";
    }
    else{
        qDebug()<<"录上了！";
    }
    m_audioRecorder->stop();
}
void audioRecorder::updateDevices()
{
    const auto currentDevice = boxValue(micChoose).value<QAudioDevice>();
    int currentDeviceIndex = 0;

    micChoose->clear();

    micChoose->addItem(tr("Default"), {});

    const QList<QAudioDevice> audioInputs = m_mediaDevices->audioInputs();
    for (const auto &device : audioInputs) {
        const auto name = device.description();
        micChoose->addItem(name, QVariant::fromValue(device));

        if (device.id() == currentDevice.id())
            currentDeviceIndex = micChoose->count() - 1;
    }
    micChoose->setCurrentIndex(currentDeviceIndex);
}
void audioRecorder::onStateChanged(QMediaRecorder::RecorderState state)
{
    switch (state) {
    case QMediaRecorder::RecordingState:
        qDebug() << "录制已开始！";
        break;
    case QMediaRecorder::StoppedState:
        qDebug() << "录制已停止";
        break;
    case QMediaRecorder::PausedState:
        qDebug() << "录制已暂停";
        break;
    }
}
QMediaFormat audioRecorder::selectedMediaFormat() const
{
    QMediaFormat format;
    format.setFileFormat(QMediaFormat::Mpeg4Audio);// always mp4a
    format.setAudioCodec(QMediaFormat::AudioCodec::AAC);// always aac
    return format;
}
