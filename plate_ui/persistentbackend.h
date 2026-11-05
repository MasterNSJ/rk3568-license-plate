#pragma once
#include <QObject>
#include <QProcess>

// A single worker process, at most one outstanding request, no frame queue.
class PersistentBackend : public QObject {
    Q_OBJECT
public:
    explicit PersistentBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~PersistentBackend() override;
    void request(quint64 id, const QString &image, const QString &directory);
    void cancel();
    void shutdown();
    void setCommand(const QString &program, const QStringList &arguments);
signals:
    void completed(quint64 id, bool success, const QString &error);
private:
    void readOutput();
    void sendPending();
    void fail(const QString &error);
    void ended(QProcess *process);
    QProcess *m_process = nullptr;
    QString m_program = QStringLiteral("/bin/sh");
    QStringList m_arguments{QStringLiteral("/userdata/plate_persistent/run_stream.sh")};
    QByteArray m_buffer, m_wire;
    QString m_error;
    quint64 m_id = 0;
    bool m_busy = false, m_ready = false, m_sent = false;
};
