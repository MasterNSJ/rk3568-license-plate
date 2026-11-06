#include <QCoreApplication>
#include <QImage>
#include <QImageWriter>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc!=3)return 2;
    QImage source(QString::fromLocal8Bit(argv[1]));
    if(source.isNull())return 3;
    source=source.scaled(2112,1568,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    QDir directory(QString::fromLocal8Bit(argv[2]));
    if(!directory.mkpath("."))return 4;
    QJsonArray records;
    for(int trial=0;trial<3;++trial)for(const QByteArray format:{QByteArray("png"),QByteArray("bmp")}) {
        const QString file=directory.filePath(QString::number(trial)+"."+QString::fromLatin1(format));
        QImageWriter writer(file,format);if(format=="png")writer.setCompression(1);
        QElapsedTimer clock;clock.start();if(!writer.write(source))return 5;
        const double ms=clock.nsecsElapsed()/1000000.0;
        clock.restart();
        const QImage decoded=QImage(file).convertToFormat(QImage::Format_RGB32);
        const double decodeMs=clock.nsecsElapsed()/1000000.0;
        const bool equal=source==decoded;
        records.append(QJsonObject{{"trial",trial},{"format",QString::fromLatin1(format)},
            {"encode_ms",ms},{"decode_ms",decodeMs},{"file_bytes",double(QFileInfo(file).size())},{"pixel_equal",equal}});
        if(!equal)return 6;
    }
    std::cout<<QJsonDocument(QJsonObject{{"width",2112},{"height",1568},{"records",records}}).toJson(QJsonDocument::Compact).constData()<<'\n';
}
