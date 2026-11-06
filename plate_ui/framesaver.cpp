#include "framesaver.h"

#include <QImageWriter>
#include <QThread>
#include <QElapsedTimer>

void FrameSaver::save(FrameSaveTaskPtr task)
{
    if (!task)
        return;
    task->error.clear();
    QElapsedTimer timer;
    timer.start();
    task->frameSize = task->frame.size();
    const auto format = task->frame.pixelFormat();
    const bool subsampled420 = format == QVideoFrame::Format_NV12
                           || format == QVideoFrame::Format_NV21
                           || format == QVideoFrame::Format_YUV420P
                           || format == QVideoFrame::Format_YV12;
    const bool packed422 = format == QVideoFrame::Format_UYVY
                        || format == QVideoFrame::Format_YUYV;
    QImage image;
    if (!task->frame.isValid() || task->frameSize.isEmpty()) {
        task->error = QStringLiteral("摄像头帧无效");
    } else if (format <= QVideoFrame::Format_Invalid || format >= QVideoFrame::NPixelFormats) {
        // Qt 5.15's converter table is indexed by the built-in pixel format.
        task->error = QStringLiteral("暂不支持该摄像头帧格式");
    } else if ((subsampled420 && (task->frame.width() % 2 || task->frame.height() % 2))
               || (packed422 && task->frame.width() % 2)) {
        task->error = QStringLiteral("摄像头帧尺寸不符合当前颜色格式要求");
    } else if (QThread::currentThread()->isInterruptionRequested()) {
        task->error = QStringLiteral("取帧任务已取消");
    } else {
        // Qt handles actual plane addresses, row stride, and map/unmap.
        // RGB is copied and YUV is converted into a separately owned QImage.
        image = task->frame.image();
        if (image.isNull())
            task->error = QStringLiteral("无法读取摄像头帧，请查看终端中的帧格式");
    }

    // The queued argument holds a shared task, not a second QVideoFrame value.
    // Release the camera buffer before the slower lossless PNG encoding.
    task->frame = QVideoFrame();
    task->convertMs = timer.nsecsElapsed() / 1000000.0;
    timer.restart();
    if (task->error.isEmpty()) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            task->error = QStringLiteral("取帧任务已取消");
        } else if (!task->directory || !task->directory->isValid() || task->imagePath.isEmpty()) {
            task->error = QStringLiteral("无法创建摄像头识别图片");
        } else {
            QImageWriter writer(task->imagePath, "png");
            writer.setCompression(1); // Lossless, favor speed for the sampled frame.
            if (!writer.write(image))
                task->error = QStringLiteral("保存摄像头图片失败：%1").arg(writer.errorString());
        }
    }
    task->encodeMs = timer.nsecsElapsed() / 1000000.0;
    emit saved(task);
}

