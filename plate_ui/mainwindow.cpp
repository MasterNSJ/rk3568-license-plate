#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCameraInfo>
#include <QCameraViewfinder>
#include <QCameraViewfinderSettings>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVideoProbe>
#include <QtMath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_backend = new PersistentBackend(this);
    connect(m_backend, &PersistentBackend::completed, this,
            [this](quint64 id, bool success, const QString &error) {
        if (!m_persistentJob || id != m_persistentId) return;
        m_persistentJob = false;
        completeImageRecognition(id, success, error);
    });

    // Keep the original label for standby/error messages; only one is visible.
    m_viewfinder = new QCameraViewfinder(ui->cameraFrame);
    m_viewfinder->setObjectName(QStringLiteral("cameraViewfinder"));
    m_viewfinder->setAspectRatioMode(Qt::KeepAspectRatio);
    m_viewfinder->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_viewfinder->setMinimumSize(1, 1);
    ui->cameraLayout->addWidget(m_viewfinder, 1);
    m_viewfinder->hide();

    m_recognizeButton = new QPushButton(QStringLiteral("识别图片"), ui->centralwidget);
    m_recognizeButton->setObjectName(QStringLiteral("recognizeImageButton"));
    m_recognizeButton->setCursor(Qt::PointingHandCursor);
    ui->buttonLayout->insertWidget(1, m_recognizeButton);
    connect(m_recognizeButton, &QPushButton::clicked, this, &MainWindow::startImageRecognition);
    ui->latencyTitleLabel->setText(QStringLiteral("模型调用"));

    qRegisterMetaType<FrameSaveTaskPtr>("FrameSaveTaskPtr");
    m_frameThread = new QThread(this);
    m_frameSaver = new FrameSaver;
    m_frameSaver->moveToThread(m_frameThread);
    connect(this, &MainWindow::saveCameraFrame, m_frameSaver, &FrameSaver::save, Qt::QueuedConnection);
    connect(m_frameSaver, &FrameSaver::saved, this, &MainWindow::handleFrameSaved, Qt::QueuedConnection);
    connect(m_frameThread, &QThread::finished, m_frameSaver, &QObject::deleteLater);
    m_frameThread->start();

    m_cameraTimer = new QTimer(this);
    m_cameraTimer->setTimerType(Qt::PreciseTimer);
    m_cameraTimer->setInterval(1000);
    connect(m_cameraTimer, &QTimer::timeout, this, &MainWindow::cameraTick);

    m_imageWatchdog = new QTimer(this);
    m_imageWatchdog->setSingleShot(true);
    m_imageWatchdog->setInterval(30000);
    connect(m_imageWatchdog, &QTimer::timeout, this, [this] {
        cancelImageRecognition(QStringLiteral("图片识别超过30秒，已取消，请检查模型和运行日志"), true);
    });

    m_startTimer = new QTimer(this);
    m_startTimer->setSingleShot(true);
    m_startTimer->setInterval(10000);
    connect(m_startTimer, &QTimer::timeout, this, [this] {
        if (m_previewRequested)
            showCameraError(QStringLiteral("启动超时，请关闭其他相机程序后重试"));
    });

    // Escape and the on-screen Exit button both close the application.
    auto *exitShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(exitShortcut, &QShortcut::activated, this, &QWidget::close);

    updateUiScale();
    updateActionButtons();
}

MainWindow::~MainWindow()
{
    m_closing = true;
    m_frameThread->requestInterruption();
    invalidateCameraSession();
    m_startTimer->stop();
    m_imageWatchdog->stop();
    ++m_imageGeneration;
    m_backend->shutdown();
    if (m_imageProcess) {
        // Only shutdown waits briefly; image processing never blocks the UI loop.
        m_imageProcess->disconnect(this);
        if (m_imageProcess->state() != QProcess::NotRunning) {
            m_imageProcess->kill();
            m_imageProcess->waitForFinished(1000);
        }
        delete m_imageProcess;
        m_imageProcess = nullptr;
    }
    if (m_videoProbe) {
        m_videoProbe->disconnect(this);
        m_videoProbe->setSource(static_cast<QMediaObject *>(nullptr));
        delete m_videoProbe;
        m_videoProbe = nullptr;
    }
    if (m_camera) {
        m_camera->disconnect(this);
        m_camera->stop();
        m_camera->unload();
        delete m_camera;
    }
    // The converter owns the only queued frame. Let its bounded save finish;
    // never destroy a running QThread or use terminate() on image conversion.
    m_frameSaver->disconnect(this);
    m_frameThread->quit();
    m_frameThread->wait();
    m_frameSaveTask.clear();
    m_activeCameraTask.clear();
    m_recentCameraTask.clear();
    delete ui;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateUiScale();
    updateImagePreview();
}

void MainWindow::updateUiScale()
{
    // The desktop preview is 400x640; the 800x1280 panel uses a scale of 2.
    const qreal scale = qBound(0.8, qMin(width() / 400.0, height() / 640.0), 3.0);
    if (qAbs(scale - m_uiScale) < 0.01)
        return;
    m_uiScale = scale;

    const auto px = [scale](int value) { return qMax(1, qRound(value * scale)); };
    const auto setTextFont = [px](QWidget *widget, int size, bool bold = false) {
        QFont textFont = widget->font();
        textFont.setPixelSize(px(size));
        textFont.setBold(bold);
        widget->setFont(textFont);
    };

    setTextFont(this, 12);
    setTextFont(ui->titleLabel, 20, true);
    setTextFont(ui->modeLabel, 11, true);
    setTextFont(ui->cameraTitleLabel, 12, true);
    setTextFont(ui->cameraStateLabel, 10);
    setTextFont(ui->cameraLabel, 17, true);
    setTextFont(ui->plateTitleLabel, 11);
    setTextFont(ui->resultStateLabel, 10);
    setTextFont(ui->plateLabel, 32, true);
    setTextFont(ui->confidenceLabel, 11);
    setTextFont(ui->ocrConfidenceLabel, 11);
    setTextFont(ui->fpsValueLabel, 18, true);
    setTextFont(ui->latencyValueLabel, 18, true);
    setTextFont(ui->inputSizeValueLabel, 18, true);
    setTextFont(ui->fpsTitleLabel, 9);
    setTextFont(ui->latencyTitleLabel, 9);
    setTextFont(ui->inputSizeTitleLabel, 9);
    setTextFont(ui->statuslabel, 11);
    setTextFont(ui->testButton, 13, true);
    setTextFont(m_recognizeButton, 13, true);
    setTextFont(ui->stopButton, 13, true);
    setTextFont(ui->exitButton, 13);


    ui->mainLayout->setContentsMargins(px(14), px(14), px(14), px(12));
    ui->mainLayout->setSpacing(px(10));
    ui->headerLayout->setSpacing(px(10));
    ui->cameraLayout->setContentsMargins(px(14), px(12), px(14), px(12));
    ui->resultLayout->setContentsMargins(px(14), px(12), px(14), px(12));
    ui->resultLayout->setSpacing(px(5));
    ui->metricsLayout->setContentsMargins(px(10), px(9), px(10), px(9));
    ui->buttonLayout->setSpacing(px(8));

    ui->headerFrame->setMinimumHeight(px(44));
    ui->cameraFrame->setMinimumHeight(px(160));
    ui->modeLabel->setMinimumWidth(px(58));
    ui->metricsFrame->setMinimumHeight(px(60));
    ui->statuslabel->setMinimumHeight(px(24));
    for (QPushButton *button : {ui->testButton, m_recognizeButton, ui->stopButton, ui->exitButton})
        button->setMinimumHeight(px(44));
}

void MainWindow::on_testButton_clicked()
{
    startCamera();
}

void MainWindow::startCamera()
{
    if (m_previewRequested)
        return;

    invalidateCameraSession();

    const auto cameras = QCameraInfo::availableCameras();
    if (cameras.isEmpty()) {
        showCameraError(QStringLiteral("未检测到摄像头，请检查连接后重试"));
        return;
    }

    QCameraInfo selectedCamera;
    for (const QCameraInfo &camera : cameras)
        qInfo() << "Camera available:" << camera.deviceName() << camera.description();

#if defined(Q_OS_LINUX)
    // The other RKISP video nodes are not interchangeable with the main path.
    for (const QCameraInfo &camera : cameras) {
        const QString name = camera.deviceName();
        if (name == QStringLiteral("/dev/video0")
                || name == QStringLiteral("/dev/video-camera0")
                || camera.description().contains(QStringLiteral("rkisp_mainpath"))) {
            selectedCamera = camera;
            break;
        }
    }
    if (selectedCamera.isNull()) {
        showCameraError(QStringLiteral("未找到主摄像头通道，请检查启动脚本与设备连接"));
        return;
    }
#else
    // Windows uses its own camera for local testing, when one is available.
    selectedCamera = QCameraInfo::defaultCamera();
    if (selectedCamera.isNull())
        selectedCamera = cameras.first();
#endif

    m_camera = new QCamera(selectedCamera, this);
    QCamera *camera = m_camera;
    connect(camera, &QCamera::statusChanged, this, [this, camera](QCamera::Status status) {
        if (m_camera == camera)
            handleCameraStatus(status);
    });
    connect(camera, QOverload<QCamera::Error>::of(&QCamera::error),
            this, [this, camera](QCamera::Error error) {
        if (m_camera == camera && error != QCamera::NoError)
            showCameraError(camera->errorString());
    });

    if (!camera->isAvailable()) {
        showCameraError(QStringLiteral("摄像头服务不可用，请检查 Qt 多媒体组件"));
        return;
    }

    camera->setViewfinder(m_viewfinder);
    camera->setCaptureMode(QCamera::CaptureViewfinder);
    // A backend can report errors synchronously while being configured.
    if (m_camera != camera)
        return;

    m_previewRequested = true;
    m_startPending = true;
    m_videoProbe = new QVideoProbe(this);
    const QPointer<QVideoProbe> probe(m_videoProbe);
    const QPointer<QCamera> cameraGuard(camera);
    connect(m_videoProbe, &QVideoProbe::videoFrameProbed, this,
            [this, probe, cameraGuard](const QVideoFrame &frame) {
        if (probe && probe == m_videoProbe && cameraGuard && cameraGuard == m_camera)
            handleVideoFrame(frame);
    });
    connect(m_videoProbe, &QVideoProbe::flush, this, [this, probe, cameraGuard] {
        if (probe && probe == m_videoProbe && cameraGuard && cameraGuard == m_camera)
            handleProbeFlush();
    });
    if (!m_videoProbe->setSource(camera))
        qWarning() << "Camera probe unavailable: preview and image recognition remain usable";
    if (m_camera != camera || !m_previewRequested)
        return;
    ui->cameraTitleLabel->setText(QStringLiteral("视频预览"));
    updateActionButtons();
    ui->modeLabel->setText(QStringLiteral("启动中"));
    ui->cameraStateLabel->setText(QStringLiteral("连接中"));
    ui->statuslabel->setText(QStringLiteral("状态：正在启动摄像头"));
    showPreviewPlaceholder(QStringLiteral("正在启动摄像头…"));
    m_startTimer->start();
    qInfo() << "Opening camera:" << selectedCamera.deviceName();
    camera->load();
}

void MainWindow::handleCameraStatus(QCamera::Status status)
{
    if (!m_previewRequested || !m_camera)
        return;

    if (status == QCamera::LoadedStatus && m_startPending) {
        m_startPending = false;
        // Request a modest preview size only when the backend advertises it.
        const QSize previewSize(640, 480);
        if (m_camera->supportedViewfinderResolutions().contains(previewSize)) {
            QCameraViewfinderSettings settings;
            settings.setResolution(previewSize);
            m_camera->setViewfinderSettings(settings);
        }
        if (!m_camera || !m_previewRequested)
            return;
        ui->cameraLabel->hide();
        m_viewfinder->show();
        m_camera->start();
    } else if (status == QCamera::ActiveStatus) {
        m_startTimer->stop();
        m_cameraRecognitionEnabled = false;
#if defined(Q_OS_LINUX)
        m_cameraRecognitionEnabled = m_videoProbe && m_videoProbe->isActive()
            && QFileInfo(QStringLiteral("/userdata/plate_persistent/run_stream.sh")).isReadable();
#endif
        m_probeFpsClock.start();
        m_probeFrameCount = 0;
        m_cameraTimer->start();
        ui->modeLabel->setText(m_imageBusy ? QStringLiteral("识别中") : QStringLiteral("预览中"));
        ui->cameraStateLabel->setText(QStringLiteral("已启动"));
        ui->statuslabel->setText(m_cameraRecognitionEnabled
            ? QStringLiteral("状态：预览中，最多每秒抽取一帧识别；完整号码仍需核对")
            : QStringLiteral("状态：摄像头预览中；自动识别不可用，可继续使用识别图片"));
        const QSize size = m_camera->viewfinderSettings().resolution();
        if (size.isValid())
            ui->inputSizeValueLabel->setText(QStringLiteral("%1×%2").arg(size.width()).arg(size.height()));
        requestCameraFrame();
    } else if (status == QCamera::UnavailableStatus) {
        showCameraError(QStringLiteral("摄像头不可用，请检查连接或设备占用"));
    }
}

void MainWindow::releaseCamera()
{
    m_startTimer->stop();
    m_previewRequested = false;
    m_startPending = false;
    invalidateCameraSession();
    if (m_videoProbe) {
        QVideoProbe *probe = m_videoProbe;
        m_videoProbe = nullptr;
        probe->disconnect(this);
        probe->setSource(static_cast<QMediaObject *>(nullptr));
        probe->deleteLater();
    }
    QCamera *camera = m_camera;
    m_camera = nullptr;
    if (!camera)
        return;

    camera->disconnect(this);
    camera->stop();
    camera->unload();
    camera->setViewfinder(static_cast<QVideoWidget *>(nullptr));
    // Errors can be emitted from inside QCamera: do not delete that sender here.
    camera->deleteLater();
}

void MainWindow::showPreviewPlaceholder(const QString &message)
{
    m_resultPixmap = QPixmap();
    m_viewfinder->hide();
    ui->cameraLabel->clear();
    ui->cameraLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->cameraLabel->setText(message);
    ui->cameraLabel->show();
}

void MainWindow::showCameraError(const QString &message)
{
    releaseCamera();
    const QString detail = message.isEmpty() ? QStringLiteral("未知错误，请重试") : message;
    updateActionButtons();
    ui->modeLabel->setText(m_imageBusy && !m_imageCancelled ? QStringLiteral("识别中") : QStringLiteral("待机"));
    ui->cameraStateLabel->setText(QStringLiteral("启动失败"));
    ui->inputSizeValueLabel->setText(QStringLiteral("--"));
    ui->fpsValueLabel->setText(QStringLiteral("--"));
    ui->statuslabel->setText(m_imageBusy && !m_imageCancelled && !m_imageFromCamera
        ? QStringLiteral("状态：摄像头启动失败，图片仍在识别；%1").arg(detail)
        : QStringLiteral("状态：%1").arg(detail));
    showPreviewPlaceholder(QStringLiteral("摄像头启动失败\n%1").arg(detail));
    qWarning() << "Camera preview failed:" << detail;
}

void MainWindow::on_stopButton_clicked()
{
    cancelImageRecognition(QStringLiteral("已取消图片识别"));
    releaseCamera();
    showPreviewPlaceholder(QStringLiteral("预览已停止\n点击开始预览重新启动"));
    updateActionButtons();
    ui->modeLabel->setText(QStringLiteral("已停止"));
    ui->cameraStateLabel->setText(QStringLiteral("已停止"));
    ui->inputSizeValueLabel->setText(QStringLiteral("--"));
    ui->fpsValueLabel->setText(QStringLiteral("--"));
    ui->statuslabel->setText(QStringLiteral("状态：已停止"));
}

void MainWindow::on_exitButton_clicked()
{
    close();
}

void MainWindow::updateActionButtons()
{
    ui->testButton->setEnabled(!m_previewRequested);
    m_recognizeButton->setEnabled(!m_imageBusy && !m_frameSaveTask && !m_imageDialogOpen);
    ui->stopButton->setEnabled(m_previewRequested || m_frameSaveTask
                              || (m_imageBusy && !m_imageCancelled));
}

void MainWindow::startImageRecognition()
{
#if !defined(Q_OS_LINUX)
    QMessageBox::information(this, QStringLiteral("图片识别"),
        QStringLiteral("图片识别使用RK3568开发板上的模型和程序。\n"
                       "请将项目交叉编译并部署到开发板后使用此按钮。\n"
                       "Windows端可继续查看界面和测试摄像头预览。"));
    return;
#else
    if (m_imageBusy || m_frameSaveTask || m_imageDialogOpen || m_closing)
        return;

    const QString script = QStringLiteral("/userdata/plate_persistent/run_stream.sh");
    if (!QFileInfo(script).isReadable()) {
        qWarning().noquote() << "Missing image recognition launcher:" << script;
        showImageRecognitionError(QStringLiteral("未找到识别程序，请先部署板端运行包"));
        return;
    }

    const QString imageDirectory = QStringLiteral("/userdata/plate_persistent/images");
    QString image;
    {
        QScopedValueRollback<bool> dialogGuard(m_imageDialogOpen, true);
        m_cameraFrameRequested = false;
        updateActionButtons();
        image = QFileDialog::getOpenFileName(this, QStringLiteral("选择车牌照片"),
            QDir(imageDirectory).exists() ? imageDirectory : QDir::homePath(),
            QStringLiteral("图片文件 (*.jpg *.jpeg *.png *.bmp);;所有文件 (*)"));
    }
    updateActionButtons();
    if (image.isEmpty())
        return;
    if (!QFileInfo(image).isFile() || !QFileInfo(image).isReadable()) {
        showImageRecognitionError(QStringLiteral("无法读取所选图片"));
        return;
    }

    QString resultsBase = QStringLiteral("/userdata/plate_ui/image_results");
    if (!QDir().mkpath(resultsBase)) {
        resultsBase = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                    + QStringLiteral("/image_results");
        if (!QDir().mkpath(resultsBase)) {
            showImageRecognitionError(QStringLiteral("无法创建图片识别结果目录"));
            return;
        }
    }
    QTemporaryDir resultDirectory(resultsBase + QStringLiteral("/image_XXXXXX"));
    if (!resultDirectory.isValid()) {
        showImageRecognitionError(QStringLiteral("无法创建独立的图片识别目录"));
        return;
    }
    // Preserve evidence after this function returns, including failed/cancelled runs.
    resultDirectory.setAutoRemove(false);
    m_imageResultDir = resultDirectory.path();
    m_imageInputPath = QFileInfo(image).absoluteFilePath();
    m_imageFromCamera = false;
    startPersistentRecognition();
#endif
}

void MainWindow::launchImageProcess(QProcess *process)
{
    if (!process)
        return;
    if (m_imageBusy || m_imageProcess) {
        process->deleteLater();
        return;
    }
    process->setParent(this);
    m_imageStderrTail.clear();
    m_imageBusy = true;
    m_imageCancelled = false;
    const quint64 generation = ++m_imageGeneration;
    m_imageClock.start();

    if (!m_imageFromCamera) {
        ui->plateLabel->setText(QStringLiteral("--"));
        ui->confidenceLabel->setText(QStringLiteral("检测置信度  --"));
        ui->ocrConfidenceLabel->setText(QStringLiteral("识别置信度  --"));
        ui->latencyValueLabel->setText(QStringLiteral("--"));
        ui->resultStateLabel->setText(QStringLiteral("识别中"));
    }
    ui->modeLabel->setText(QStringLiteral("识别中"));
    if (!m_imageFromCamera)
        ui->statuslabel->setText(QStringLiteral("状态：正在识别所选图片"));
    updateActionButtons();

    m_imageProcess = process;
    const QPointer<QProcess> guard(process);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, guard] {
        if (guard && guard == m_imageProcess)
            readImageProcessOutput(guard);
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, guard] {
        if (guard && guard == m_imageProcess)
            readImageProcessOutput(guard);
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, guard, generation](QProcess::ProcessError error) {
        if (!guard || guard != m_imageProcess)
            return;
        if (error == QProcess::FailedToStart)
            finishImageRecognition(guard, generation, false, guard->errorString());
        else
            qWarning() << "Image recognition process error:" << guard->errorString();
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, guard, generation](int exitCode, QProcess::ExitStatus status) {
        if (!guard || guard != m_imageProcess)
            return;
        const bool success = status == QProcess::NormalExit && exitCode == 0;
        finishImageRecognition(guard, generation, success,
            success ? QString() : QStringLiteral("识别程序异常结束，退出码%1").arg(exitCode));
    });
    qInfo().noquote() << "Image recognition input:" << m_imageInputPath;
    qInfo().noquote() << "Image recognition results:" << m_imageResultDir;
    m_imageWatchdog->start();
    process->start();
}

void MainWindow::startPersistentRecognition()
{
    if (m_imageBusy || m_closing) return;
    m_imageStderrTail.clear();
    m_imageBusy = true; m_imageCancelled = false; m_persistentJob = true;
    m_persistentId = ++m_imageGeneration;
    m_imageClock.start();
    if (!m_imageFromCamera) {
        ui->plateLabel->setText(QStringLiteral("--"));
        ui->confidenceLabel->setText(QStringLiteral("检测置信度  --"));
        ui->ocrConfidenceLabel->setText(QStringLiteral("识别置信度  --"));
        ui->latencyValueLabel->setText(QStringLiteral("--"));
        ui->resultStateLabel->setText(QStringLiteral("识别中"));
        ui->statuslabel->setText(QStringLiteral("状态：正在识别所选图片"));
    }
    ui->modeLabel->setText(QStringLiteral("识别中"));
    updateActionButtons();
    qInfo().noquote() << "Persistent recognition input:" << m_imageInputPath;
    m_imageWatchdog->start();
    m_backend->request(m_persistentId, m_imageInputPath, m_imageResultDir);
}

void MainWindow::readImageProcessOutput(QProcess *process)
{
    if (!process || process != m_imageProcess)
        return;
    const QByteArray output = process->readAllStandardOutput();
    const QByteArray errors = process->readAllStandardError();
    if (!output.isEmpty())
        qInfo().noquote() << QString::fromUtf8(output).trimmed();
    if (!errors.isEmpty()) {
        const QString detail = QString::fromUtf8(errors);
        m_imageStderrTail = (m_imageStderrTail + detail).right(8192);
        qWarning().noquote() << detail.trimmed();
    }
}

void MainWindow::finishImageRecognition(QProcess *process, quint64 generation,
                                      bool success, const QString &errorMessage)
{
    // FailedToStart and finished may both arrive: only the active process may finish once.
    if (!process || process != m_imageProcess || !m_imageBusy)
        return;
    readImageProcessOutput(process);
    m_imageProcess = nullptr;
    process->disconnect(this);
    process->deleteLater();
    completeImageRecognition(generation, success, errorMessage);
}

void MainWindow::completeImageRecognition(quint64 generation, bool success, const QString &errorMessage)
{
    const bool discard = m_imageCancelled || generation != m_imageGeneration
        || (m_imageFromCamera && (m_imageCameraGeneration != m_cameraGeneration
                                  || !m_previewRequested || m_closing));
    m_imageWatchdog->stop();
    m_imageBusy = false;
    updateActionButtons();
    qInfo() << "Image recognition process elapsed ms:" << m_imageClock.elapsed();
    qInfo().noquote() << (m_imageFromCamera ? "Camera recognition temporary result:" : "Image recognition files retained:")
                     << m_imageResultDir;
    if (discard) {
        finishCameraTask(false);
        return;
    }

    if (!success) {
        if (!m_imageStderrTail.trimmed().isEmpty())
            qWarning().noquote() << "Image recognition stderr:" << m_imageStderrTail.trimmed();
        showImageRecognitionError(errorMessage);
        finishCameraTask(true);
        return;
    }
    QString error;
    if (!applyImageRecognitionResult(&error))
        showImageRecognitionError(error);
    finishCameraTask(true);
}

void MainWindow::cancelImageRecognition(const QString &message, bool timedOut)
{
    if (!m_imageBusy || (!m_imageProcess && !m_persistentJob) || m_imageCancelled)
        return;
    m_imageCancelled = true;
    if (timedOut && m_imageFromCamera)
        m_cameraRecognitionEnabled = false;
    ++m_imageGeneration;
    m_imageWatchdog->stop();
    ui->resultStateLabel->setText(timedOut ? QStringLiteral("识别超时") : QStringLiteral("已取消"));
    ui->modeLabel->setText(m_previewRequested ? QStringLiteral("预览中") : QStringLiteral("待机"));
    ui->statuslabel->setText(QStringLiteral("状态：%1").arg(message));
    updateActionButtons();
    qWarning().noquote() << message << "Results:" << m_imageResultDir;
    if (m_persistentJob) {
        m_backend->cancel();
        return;
    }
    const QPointer<QProcess> guard(m_imageProcess);
    m_imageProcess->terminate();
    QTimer::singleShot(500, this, [this, guard] {
        if (guard && guard == m_imageProcess && guard->state() != QProcess::NotRunning)
            guard->kill();
    });
}

bool MainWindow::applyImageRecognitionResult(QString *error)
{
    QFile file(QDir(m_imageResultDir).filePath(QStringLiteral("results.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "Cannot read recognition result:" << file.fileName() << file.errorString();
        *error = QStringLiteral("无法读取识别结果，请查看终端输出");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "Invalid recognition JSON:" << parseError.errorString() << parseError.offset;
        *error = QStringLiteral("识别结果格式有误，请查看终端输出");
        return false;
    }
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("detections")).isArray()
            || !root.value(QStringLiteral("detection_call_ms")).isDouble()) {
        *error = QStringLiteral("识别结果缺少检测数据或模型调用时间");
        return false;
    }
    const QJsonArray detections = root.value(QStringLiteral("detections")).toArray();
    double modelCallMs = root.value(QStringLiteral("detection_call_ms")).toDouble(-1);
    if (!qIsFinite(modelCallMs) || modelCallMs < 0) {
        *error = QStringLiteral("识别结果中的模型调用时间无效");
        return false;
    }
    QJsonObject best;
    double bestConfidence = -1;
    int completeCount = 0;
    for (const QJsonValue &value : detections) {
        if (!value.isObject()) {
            *error = QStringLiteral("识别结果中的车牌数据无效");
            return false;
        }
        const QJsonObject detection = value.toObject();
        if (!detection.value(QStringLiteral("raw_plate")).isString()
                || !detection.value(QStringLiteral("basic_complete")).isBool()
                || !detection.value(QStringLiteral("detection_confidence")).isDouble()
                || !detection.value(QStringLiteral("recognition_call_ms")).isDouble()) {
            *error = QStringLiteral("识别结果中的车牌字段不完整");
            return false;
        }
        const double confidence = detection.value(QStringLiteral("detection_confidence")).toDouble(-1);
        const double recognitionMs = detection.value(QStringLiteral("recognition_call_ms")).toDouble(-1);
        if (!qIsFinite(confidence) || confidence < 0 || confidence > 1
                || !qIsFinite(recognitionMs) || recognitionMs < 0) {
            *error = QStringLiteral("识别结果中的分数或时间无效");
            return false;
        }
        modelCallMs += recognitionMs;
        const QString raw = detection.value(QStringLiteral("raw_plate")).toString();
        const QString reviewReason = detection.value(QStringLiteral("review_reason")).toString();
        const bool complete = detection.value(QStringLiteral("basic_complete")).toBool()
                           && !raw.isEmpty() && reviewReason.isEmpty();
        qInfo().noquote() << "Plate candidate:" << raw << "basic complete:" << complete
                         << "detection confidence:" << confidence << "review:" << reviewReason;
        if (complete) {
            ++completeCount;
            if (confidence > bestConfidence) {
                bestConfidence = confidence;
                best = detection;
            }
        }
    }

    ui->plateLabel->setText(QStringLiteral("--"));
    ui->confidenceLabel->setText(QStringLiteral("检测置信度  --"));
    ui->ocrConfidenceLabel->setText(QStringLiteral("识别置信度  --"));
    ui->latencyValueLabel->setText(QStringLiteral("%1 ms").arg(modelCallMs, 0, 'f', 1));
    ui->modeLabel->setText(m_previewRequested ? QStringLiteral("预览中") : QStringLiteral("待机"));
    QString resultStatus;
    if (detections.isEmpty()) {
        ui->resultStateLabel->setText(QStringLiteral("未检测到"));
        resultStatus = QStringLiteral("未检测到车牌");
    } else if (best.isEmpty()) {
        ui->plateLabel->setText(QStringLiteral("待确认"));
        ui->resultStateLabel->setText(QStringLiteral("待确认"));
        resultStatus = QStringLiteral("%1个候选的号码不完整，需要人工确认").arg(detections.size());
    } else {
        ui->plateLabel->setText(best.value(QStringLiteral("raw_plate")).toString());
        ui->confidenceLabel->setText(QStringLiteral("检测置信度  %1%")
                                    .arg(bestConfidence * 100.0, 0, 'f', 1));
        ui->resultStateLabel->setText(QStringLiteral("待核对"));
        resultStatus = QStringLiteral("%1个候选、%2个号码完整，显示定位分数最高的完整号码")
                       .arg(detections.size()).arg(completeCount);
    }
    if (m_imageFromCamera)
        setCameraRecognitionStatus(QStringLiteral("状态：最多每秒识别一帧；%1，请核对真实号码").arg(resultStatus));
    else
        ui->statuslabel->setText(QStringLiteral("状态：%1").arg(resultStatus));

    // Never replace the live viewfinder with a photo; results remain in their card.
    if (!m_previewRequested && !m_imageFromCamera) {
        m_resultPixmap.load(QDir(m_imageResultDir).filePath(QStringLiteral("detection.jpg")));
        if (m_resultPixmap.isNull())
            m_resultPixmap.load(m_imageInputPath);
        if (!m_resultPixmap.isNull()) {
            ui->cameraTitleLabel->setText(QStringLiteral("图片结果"));
            m_viewfinder->hide();
            ui->cameraLabel->clear();
            ui->cameraLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            ui->cameraLabel->show();
            updateImagePreview();
            QTimer::singleShot(0, this, &MainWindow::updateImagePreview);
        }
        const QSize size = QImageReader(m_imageInputPath).size();
        if (size.isValid())
            ui->inputSizeValueLabel->setText(QStringLiteral("%1×%2").arg(size.width()).arg(size.height()));
    }
    return true;
}

void MainWindow::showImageRecognitionError(const QString &message)
{
    if (m_imageFromCamera)
        m_cameraRecognitionEnabled = false;
    ui->plateLabel->setText(QStringLiteral("--"));
    ui->confidenceLabel->setText(QStringLiteral("检测置信度  --"));
    ui->ocrConfidenceLabel->setText(QStringLiteral("识别置信度  --"));
    ui->latencyValueLabel->setText(QStringLiteral("--"));
    ui->resultStateLabel->setText(QStringLiteral("识别失败"));
    ui->modeLabel->setText(m_previewRequested ? QStringLiteral("预览中") : QStringLiteral("待机"));
    ui->statuslabel->setText(m_imageFromCamera
        ? QStringLiteral("状态：自动识别已暂停，预览仍可使用；%1").arg(message)
        : QStringLiteral("状态：%1").arg(message));
    updateActionButtons();
    qWarning().noquote() << "Image recognition failed:" << message;
}

void MainWindow::updateImagePreview()
{
    if (m_previewRequested || m_resultPixmap.isNull() || !ui->cameraLabel->isVisible())
        return;
    const QSize size = ui->cameraLabel->contentsRect().size();
    if (size.width() > 0 && size.height() > 0)
        ui->cameraLabel->setPixmap(m_resultPixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::invalidateCameraSession()
{
    ++m_cameraGeneration;
    m_cameraRecognitionEnabled = false;
    m_cameraFrameRequested = false;
    if (m_cameraTimer)
        m_cameraTimer->stop();
    m_cameraSampleClock.invalidate();
    m_probeFpsClock.invalidate();
    m_probeFrameCount = 0;
    m_probeFirstFrameLogged = false;
    // Keep a task that is currently converting alive until its saved callback.
    // Its generation is now stale, so it cannot launch another recognizer.
    if (m_imageFromCamera && m_imageBusy && !m_closing)
        cancelImageRecognition(QStringLiteral("摄像头会话已结束，已取消旧帧识别"));
}

void MainWindow::handleProbeFlush()
{
    const bool resume = m_cameraRecognitionEnabled;
    invalidateCameraSession();
    if (m_previewRequested && m_camera && m_camera->status() == QCamera::ActiveStatus) {
        m_cameraRecognitionEnabled = resume;
        m_probeFpsClock.start();
        m_cameraTimer->start();
        // Only a later timer tick can arm sampling after a flush.
        m_cameraSampleClock.start();
    }
}

void MainWindow::cameraTick()
{
    if (m_closing || !m_previewRequested || !m_camera
            || m_camera->status() != QCamera::ActiveStatus)
        return;
    if (m_probeFpsClock.isValid()) {
        const qint64 elapsed = m_probeFpsClock.restart();
        if (m_probeFirstFrameLogged && elapsed > 0)
            ui->fpsValueLabel->setText(QString::number(m_probeFrameCount * 1000.0 / elapsed, 'f', 1));
        else
            ui->fpsValueLabel->setText(QStringLiteral("--"));
        m_probeFrameCount = 0;
    }
    requestCameraFrame();
}

void MainWindow::requestCameraFrame()
{
#if defined(Q_OS_LINUX)
    if (m_closing || !m_cameraRecognitionEnabled || !m_previewRequested
            || !m_camera || m_camera->status() != QCamera::ActiveStatus
            || !m_videoProbe || !m_videoProbe->isActive()
            || m_imageBusy || m_frameSaveTask || m_imageDialogOpen)
        return;
    m_cameraFrameRequested = true;
#endif
}

void MainWindow::handleVideoFrame(const QVideoFrame &frame)
{
    if (m_closing || !m_previewRequested || !frame.isValid())
        return;
    ++m_probeFrameCount;
    if (!m_probeFirstFrameLogged) {
        m_probeFirstFrameLogged = true;
        qInfo() << "First probed camera frame:" << frame.pixelFormat() << frame.size()
                << "handle:" << frame.handleType();
        ui->inputSizeValueLabel->setText(QStringLiteral("%1×%2")
            .arg(frame.width()).arg(frame.height()));
    }
#if defined(Q_OS_LINUX)
    if (!m_cameraFrameRequested || !m_cameraRecognitionEnabled || m_imageBusy
            || m_frameSaveTask || m_imageDialogOpen || !m_camera
            || m_camera->status() != QCamera::ActiveStatus)
        return;
    if (m_cameraSampleClock.isValid() && m_cameraSampleClock.elapsed() < 1000)
        return;
    m_cameraFrameRequested = false;

    QString base = QStringLiteral("/userdata/plate_ui/camera_work");
    if (!QDir().mkpath(base))
        base = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                    .filePath(QStringLiteral("plate_ui_camera"));
    if (!QDir().mkpath(base)) {
        m_cameraRecognitionEnabled = false;
        setCameraRecognitionStatus(QStringLiteral("状态：无法创建摄像头识别临时目录，自动识别已暂停"));
        return;
    }
    const auto directory = QSharedPointer<QTemporaryDir>::create(base + QStringLiteral("/frame_XXXXXX"));
    if (!directory->isValid()) {
        m_cameraRecognitionEnabled = false;
        setCameraRecognitionStatus(QStringLiteral("状态：无法创建独立帧目录，自动识别已暂停"));
        return;
    }
    auto task = FrameSaveTaskPtr::create();
    task->frame = frame;
    task->frameSize = frame.size();
    task->directory = directory;
    task->generation = m_cameraGeneration;
    task->imagePath = QDir(directory->path()).filePath(QStringLiteral("frame.png"));
    m_frameSaveTask = task;
    m_cameraSampleClock.start();
    updateActionButtons();
    emit saveCameraFrame(task);
#endif
}

void MainWindow::handleFrameSaved(FrameSaveTaskPtr task)
{
    if (!task || task != m_frameSaveTask)
        return;
    m_frameSaveTask.clear();
    updateActionButtons();
    if (m_closing || task->generation != m_cameraGeneration || !m_previewRequested
            || !m_cameraRecognitionEnabled || m_imageBusy || m_imageDialogOpen)
        return;
    if (!task->error.isEmpty() || !QFileInfo(task->imagePath).isReadable()) {
        m_cameraRecognitionEnabled = false;
        ui->resultStateLabel->setText(QStringLiteral("取帧失败"));
        ui->statuslabel->setText(QStringLiteral("状态：无法转换摄像头帧，自动识别已暂停，预览仍可使用"));
        qWarning().noquote() << "Frame conversion failed:" << task->error;
        return;
    }
#if defined(Q_OS_LINUX)
    const QString script = QStringLiteral("/userdata/plate_persistent/run_stream.sh");
    if (!QFileInfo(script).isReadable()) {
        m_cameraRecognitionEnabled = false;
        setCameraRecognitionStatus(QStringLiteral("状态：未找到板端识别程序，预览和识别图片仍可使用"));
        return;
    }
    m_activeCameraTask = task;
    m_imageFromCamera = true;
    m_imageCameraGeneration = task->generation;
    m_imageInputPath = task->imagePath;
    m_imageResultDir = QDir(task->directory->path()).filePath(QStringLiteral("result"));
    startPersistentRecognition();
#endif
}

void MainWindow::setCameraRecognitionStatus(const QString &text)
{
    ui->statuslabel->setText(text);
}

void MainWindow::finishCameraTask(bool keepResult)
{
    if (!m_imageFromCamera)
        return;
    if (keepResult)
        m_recentCameraTask = m_activeCameraTask;
    m_activeCameraTask.clear();
    m_imageFromCamera = false;
    updateActionButtons();
}
