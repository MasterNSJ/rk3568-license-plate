#ifndef FRAMESAVER_H
#define FRAMESAVER_H

#include <QObject>
#include <QSharedPointer>
#include <QTemporaryDir>
#include <QVideoFrame>

struct FrameSaveTask {
    QVideoFrame frame;
    QSharedPointer<QTemporaryDir> directory;
    quint64 generation = 0;
    QString imagePath;
    QSize frameSize;
    QString error;
};
using FrameSaveTaskPtr = QSharedPointer<FrameSaveTask>;
Q_DECLARE_METATYPE(FrameSaveTaskPtr)

class FrameSaver : public QObject
{
    Q_OBJECT
public:
    explicit FrameSaver(QObject *parent = nullptr) : QObject(parent) {}
public slots:
    void save(FrameSaveTaskPtr task);
signals:
    void saved(FrameSaveTaskPtr task);
};

#endif
