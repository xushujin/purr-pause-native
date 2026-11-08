#include "installation.h"
#include "media.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QSaveFile>
#include <QSslSocket>
#include <QSysInfo>
#include <QTimer>
#include <QWidget>
#include <QVBoxLayout>
#include <functional>

namespace purr {
namespace {
class InstallationCheck final : public QObject {
public:
    InstallationCheck(QString assets,QString docs,QString path)
        : QObject(qApp),assets_(std::move(assets)),docs_(std::move(docs)),reportPath_(std::move(path)),player_(this) {
        report_={{"version",QStringLiteral(PURR_VERSION)},{"architecture",QSysInfo::currentCpuArchitecture()},
                 {"qtVersion",QString::fromLatin1(qVersion())},{"platform",QGuiApplication::platformName()},
                 {"executable",QCoreApplication::applicationFilePath()},{"assets",assets_},{"docs",docs_}};
        connect(&player_,&VideoPlayer::frameAvailable,this,[this] { decoded(); });
        connect(&player_,&VideoPlayer::failed,this,[this](const QString &error) { finish(QStringLiteral("视频解码失败：")+error); });
        timeout_.setSingleShot(true); timeout_.setInterval(15000);
        connect(&timeout_,&QTimer::timeout,this,[this] { finish(QStringLiteral("视频验证超时")); });
        QTimer::singleShot(0,this,[this] { start(); });
    }
private:
    void start() {
        if (assets_.isEmpty() || docs_.isEmpty()) return finish(QStringLiteral("安装目录缺少资源或文档；不允许回退到源码目录"));
        if (!QSslSocket::supportsSsl()) return finish(QStringLiteral("没有可用的 TLS 后端"));
        report_.insert(QStringLiteral("tlsBackend"),QSslSocket::activeBackend());
        report_.insert(QStringLiteral("tlsLibrary"),QSslSocket::sslLibraryVersionString());
        const QStringList required{QStringLiteral("images/logo.png"),QStringLiteral("images/tray-icon.png"),
            QStringLiteral("images/rocket.svg"),QStringLiteral("readme/purr-pause-素材说明.txt"),
            QStringLiteral("readme/purr-pause-消息提醒素材说明.txt")};
        for (const auto &file:required) if (!QFileInfo(assets_+QLatin1Char('/')+file).isFile()) return finish(QStringLiteral("安装资源缺失：")+file);
        QPixmap icon(assets_+QStringLiteral("/images/logo.png"));
        if (icon.isNull()) return finish(QStringLiteral("应用图标解码失败"));
        QFile spec(docs_+QStringLiteral("/message-api-spec.md"));
        if (!spec.open(QIODevice::ReadOnly) || spec.size()<100) return finish(QStringLiteral("接口规范缺失或为空"));
        window_.setWindowTitle(QStringLiteral("PurrPause installation verification"));
        window_.resize(440,300);
        auto *layout=new QVBoxLayout(&window_);
        layout->addWidget(new QLabel(QStringLiteral("PurrPause ")+QStringLiteral(PURR_VERSION),&window_));
        preview_=new QLabel(&window_); preview_->setAlignment(Qt::AlignCenter); layout->addWidget(preview_,1);
        window_.show();
        next();
    }
    void next() {
        const QStringList videos{QStringLiteral("cat-walk.webm"),QStringLiteral("cat-rest.webm")};
        if (index_>=videos.size()) return finish();
        checking_=false; filename_=videos.at(index_);
        timeout_.start();
        player_.play(assets_+QStringLiteral("/webm/")+filename_,false);
    }
    void decoded() {
        if (finished_ || checking_) return;
        const QImage frame=player_.frame(); if (frame.isNull()) return;
        checking_=true; timeout_.stop();
        bool transparent=false,opaque=false;
        for (int y=0;y<frame.height();y+=8) for (int x=0;x<frame.width();x+=8) {
            const int alpha=frame.pixelColor(x,y).alpha(); transparent|=alpha<10; opaque|=alpha>245;
        }
        if (!transparent || !opaque) return finish(QStringLiteral("内置视频透明通道丢失：")+filename_);
        player_.stop();
        preview_->setPixmap(QPixmap::fromImage(frame).scaled(400,230,Qt::KeepAspectRatio,Qt::SmoothTransformation));
        videos_.append(QJsonObject{{"file",filename_},{"width",frame.width()},{"height",frame.height()},
                                  {"transparent",transparent},{"opaque",opaque}});
        // Let the native GUI actually paint the decoded frame before proceeding.
        QTimer::singleShot(100,this,[this] {
            if (window_.grab().isNull()) return finish(QStringLiteral("原生窗口绘制失败"));
            ++index_; next();
        });
    }
    void finish(const QString &failure={}) {
        if (finished_) return;
        finished_=true; timeout_.stop(); player_.stop();
        report_.insert(QStringLiteral("ok"),failure.isEmpty());
        report_.insert(QStringLiteral("videos"),videos_);
        if (!failure.isEmpty()) report_.insert(QStringLiteral("error"),failure);
        int code=failure.isEmpty()?0:2;
        QByteArray bytes=QJsonDocument(report_).toJson(QJsonDocument::Indented);
        if (!reportPath_.isEmpty()) {
            QSaveFile output(reportPath_);
            if (!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit()) code=3;
        }
        QFile output;
        if (output.open(stdout,QIODevice::WriteOnly)) { output.write(bytes); output.flush(); }
        window_.hide();
        // Destroy native windows and the decoder while QApplication is still
        // alive, after the current signal/callback has returned.
        QTimer::singleShot(0,qApp,[this,code] { delete this; qApp->exit(code); });
    }
    QString assets_,docs_,reportPath_,filename_;
    VideoPlayer player_;
    QTimer timeout_;
    QWidget window_;
    QLabel *preview_=nullptr;
    QJsonObject report_;
    QJsonArray videos_;
    int index_=0;
    bool checking_=false,finished_=false;
};
}
void verifyInstallation(const QString &assets,const QString &docs,const QString &reportPath) {
    new InstallationCheck(assets,docs,reportPath);
}
}
