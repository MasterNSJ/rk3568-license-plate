#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCamera>
#include <QElapsedTimer>
#include <QPixmap>
#include "framesaver.h"
#include "persistentbackend.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QCameraViewfinder;
class QProcess;
class QPushButton;
class QTimer;
class QThread;
class QVideoProbe;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void saveCameraFrame(FrameSaveTaskPtr task);

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void on_testButton_clicked();
    void on_stopButton_clicked();
    void on_exitButton_clicked();

private:
    void updateUiScale();
    void startCamera();
    void releaseCamera();
    void handleCameraStatus(QCamera::Status status);
    void showCameraError(const QString &message);
    void showPreviewPlaceholder(const QString &message);
    void startImageRecognition();
    void launchImageProcess(QProcess *process);
    void startPersistentRecognition();
    void completeImageRecognition(quint64 generation, bool success, const QString &errorMessage);
    void finishImageRecognition(QProcess *process, quint64 generation,
                                bool success, const QString &errorMessage);
    void readImageProcessOutput(QProcess *process);
    void cancelImageRecognition(const QString &message, bool timedOut = false);
    bool applyImageRecognitionResult(QString *error);
    void showImageRecognitionError(const QString &message);
    void updateActionButtons();
    void updateImagePreview();
    void handleVideoFrame(const QVideoFrame &frame);
    void handleFrameSaved(FrameSaveTaskPtr task);
    void handleProbeFlush();
    void cameraTick();
    void requestCameraFrame();
    void invalidateCameraSession();
    void setCameraRecognitionStatus(const QString &text);
    void finishCameraTask(bool keepResult);

    Ui::MainWindow *ui;
    qreal m_uiScale = 0.0;
    QCamera *m_camera = nullptr;
    QCameraViewfinder *m_viewfinder = nullptr;
    QTimer *m_startTimer = nullptr;
    bool m_previewRequested = false;
    bool m_startPending = false;
    QPushButton *m_recognizeButton = nullptr;
    QProcess *m_imageProcess = nullptr;
    QTimer *m_imageWatchdog = nullptr;
    QElapsedTimer m_imageClock;
    QPixmap m_resultPixmap;
    QString m_imageResultDir;
    QString m_imageInputPath;
    QString m_imageStderrTail;
    quint64 m_imageGeneration = 0;
    bool m_imageBusy = false;
    bool m_imageCancelled = false;
    QVideoProbe *m_videoProbe = nullptr;
    QThread *m_frameThread = nullptr;
    FrameSaver *m_frameSaver = nullptr;
    QTimer *m_cameraTimer = nullptr;
    FrameSaveTaskPtr m_frameSaveTask;
    FrameSaveTaskPtr m_activeCameraTask;
    FrameSaveTaskPtr m_recentCameraTask;
    QElapsedTimer m_probeFpsClock;
    QElapsedTimer m_cameraSampleClock;
    qint64 m_probeFrameCount = 0;
    quint64 m_cameraGeneration = 0;
    quint64 m_imageCameraGeneration = 0;
    bool m_cameraRecognitionEnabled = false;
    bool m_cameraFrameRequested = false;
    bool m_imageDialogOpen = false;
    bool m_imageFromCamera = false;
    bool m_probeFirstFrameLogged = false;
    bool m_closing = false;
    PersistentBackend *m_backend = nullptr;
    bool m_persistentJob = false;
    quint64 m_persistentId = 0;
};

#endif // MAINWINDOW_H
