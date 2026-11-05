#include "persistentbackend.h"
#include <QDebug>
#include <QTimer>
#include <QtMath>

PersistentBackend::~PersistentBackend() { shutdown(); }
void PersistentBackend::setCommand(const QString &program, const QStringList &arguments)
{
    Q_ASSERT(!m_process);
    m_program = program; m_arguments = arguments;
}
void PersistentBackend::request(quint64 id, const QString &image, const QString &directory)
{
    Q_ASSERT(!m_busy);
    if (m_busy) return;
    m_id = id; m_busy = true; m_sent = false; m_error.clear();
    m_wire = QByteArray::number(id) + '\t' + image.toUtf8().toHex() + '\t'
           + directory.toUtf8().toHex() + '\n';
    if (m_process) { sendPending(); return; }
    m_buffer.clear(); m_ready = false;
    auto *process = new QProcess(this);
    m_process = process;
    process->setProgram(m_program); process->setArguments(m_arguments);
    connect(process, &QProcess::readyReadStandardOutput, this, &PersistentBackend::readOutput);
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        if (process == m_process)
            qWarning().noquote() << QString::fromUtf8(process->readAllStandardError()).trimmed();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process == m_process && error == QProcess::FailedToStart) {
            m_error = process->errorString(); ended(process);
        }
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int, QProcess::ExitStatus) { ended(process); });
    process->start();
}
void PersistentBackend::sendPending()
{
    if (!m_ready || !m_busy || m_sent || !m_error.isEmpty()) return;
    m_sent = true;
    if (m_process->write(m_wire) != m_wire.size()) fail(QStringLiteral("无法发送识别请求"));
}
void PersistentBackend::readOutput()
{
    if (!m_process) return;
    m_buffer += m_process->readAllStandardOutput();
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        if (newline > 65536) { fail(QStringLiteral("识别协议行过长")); return; }
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (!line.startsWith("@PLATE_")) {
            if (!line.isEmpty()) qInfo().noquote() << QString::fromUtf8(line);
            continue;
        }
        const auto fields = line.split('\t');
        if (line == "@PLATE_READY\t1" && !m_ready) {
            m_ready = true;
            qInfo() << "Persistent models ready; PID:" << m_process->processId();
            sendPending(); continue;
        }
        bool validId = false;
        const quint64 id = fields.size() > 1 ? fields[1].toULongLong(&validId) : 0;
        if (fields.size() != 3 || !validId || !m_busy || !m_sent || id != m_id) {
            fail(QStringLiteral("识别协议或请求编号不匹配")); return;
        }
        if (fields[0] == "@PLATE_ERROR") {
            fail(QStringLiteral("板端识别失败：%1").arg(QString::fromUtf8(QByteArray::fromHex(fields[2]))));
            return;
        }
        bool validTime = false;
        const double ms = fields[2].toDouble(&validTime);
        if (fields[0] != "@PLATE_DONE" || !validTime || !qIsFinite(ms) || ms < 0) {
            fail(QStringLiteral("识别完成记录无效")); return;
        }
        qInfo() << "Persistent request" << id << "read/process/save ms:" << ms
                << "PID:" << m_process->processId();
        m_busy = false;
        emit completed(id, true, QString());
        // completed may close this service; do not use a process captured before it.
        if (!m_process) return;
    }
    if (m_buffer.size() > 65536) fail(QStringLiteral("识别输出过长"));
}
void PersistentBackend::fail(const QString &error)
{
    if (m_error.isEmpty()) m_error = error;
    if (m_process) m_process->kill();
}
void PersistentBackend::cancel() { fail(QStringLiteral("识别任务已取消")); }
void PersistentBackend::ended(QProcess *process)
{
    if (process != m_process) return;
    readOutput();
    if (process != m_process) return;
    // On DONE the busy flag is already false; idle exits need no UI callback.
    const bool busy = m_busy;
    const auto id = m_id;
    const QString error = m_error.isEmpty() ? QStringLiteral("识别进程意外退出") : m_error;
    m_process = nullptr; m_ready = false; m_busy = false;
    m_buffer.clear(); process->disconnect(this); process->deleteLater();
    if (busy) emit completed(id, false, error);
}
void PersistentBackend::shutdown()
{
    if (!m_process) return;
    auto *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        process->kill(); process->waitForFinished(2000);
    }
    delete process; m_busy = false; m_ready = false;
}
