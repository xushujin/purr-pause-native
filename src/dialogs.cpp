#include "dialogs.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QFontInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

namespace purr {
namespace {
const char dialogStyle[] = R"(
QDialog#purrDialog { background:transparent; }
QWidget { color:#3d2200; font-size:14px; }
QWidget#documentBody { background:transparent; }
QLabel { background:transparent; }
QLabel#heading { color:white; font-size:19px; font-weight:700; padding:0; }
QLabel#hint { color:#8b5e34; font-size:12px; }
QFrame#panel { background:rgba(255,255,255,235); border:0; border-radius:12px; }
QScrollArea { border:0; background:transparent; }
QWidget#scrollBody, QScrollArea > QWidget > QWidget { background:transparent; }
QLineEdit, QPlainTextEdit, QSpinBox, QComboBox { background:white; border:1px solid #f4a460; border-radius:7px; padding:7px; selection-background-color:#f4a460; }
QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QComboBox:focus { border:2px solid #e07b20; padding:6px; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color:#999; background:#f4f0eb; }
QPushButton { background:white; color:#e07b20; border:1px solid #e7b58f; border-radius:8px; padding:8px 16px; font-weight:700; }
QPushButton:hover { background:#fff5e9; border-color:#e07b20; }
QPushButton:pressed { background:#ffe6cc; }
QPushButton:disabled { color:#b8a290; border-color:#ddd; background:#f7f5f1; }
QPushButton#primary { background:white; border:1px solid #e7b58f; color:#e07b20; font-weight:700; }
QPushButton#primary:hover { background:#fff5e9; border-color:#e07b20; }
QCheckBox { spacing:9px; background:transparent; }
QCheckBox::indicator { width:18px; height:18px; border:1px solid #e7a775; border-radius:3px; background:white; }
QCheckBox::indicator:checked { background:#e07b20; border-color:#e07b20; }
QCheckBox::indicator:disabled { background:#f0ebe5; border-color:#ccc; }
QListWidget { background:rgba(255,255,255,235); border:0; border-radius:12px; padding:8px; outline:0; }
QListWidget::item { border:1px solid #f1d1b6; border-radius:8px; margin:4px; }
QListWidget::item:hover, QListWidget::item:selected { background:#fff4e6; border-color:#e07b20; }
QTableWidget { background:#fffaf5; border:0; border-radius:10px; gridline-color:#f3ddcb; }
QHeaderView::section { background:#f9dfca; color:#5a3e1b; padding:10px; border:0; font-weight:700; }
QScrollBar:vertical { background:#f1f1f1; width:15px; margin:15px 0; }
QScrollBar::handle:vertical { background:#c1c1c1; border:2px solid #f1f1f1; min-height:20px; }
QScrollBar::add-line:vertical { height:15px; subcontrol-origin:margin; subcontrol-position:bottom; background:#f1f1f1; }
QScrollBar::sub-line:vertical { height:15px; subcontrol-origin:margin; subcontrol-position:top; background:#f1f1f1; }
)";

class CheckBox : public QCheckBox {
public:
    explicit CheckBox(QWidget *parent = nullptr) : QCheckBox(parent) { setFixedSize(18,18); setCursor(Qt::PointingHandCursor); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(isChecked() ? QColor("#0075ff") : QColor("#767676"),1));
        painter.setBrush(isChecked() ? QColor("#0075ff") : Qt::white);
        painter.drawRoundedRect(QRectF(.5,.5,17,17),2,2);
        if (isChecked()) {
            painter.setPen(QPen(Qt::white,2.1,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            painter.drawPolyline(QPolygonF{QPointF(4,9),QPointF(7,12),QPointF(14,5)});
        }
        if (hasFocus()) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(QColor("#e07b20"),1,Qt::DotLine)); painter.drawRect(rect().adjusted(1,1,-1,-1)); }
    }
};

class Switch final : public QCheckBox {
public:
    explicit Switch(QWidget *parent = nullptr) : QCheckBox(parent) { setFixedSize(40,22); setCursor(Qt::PointingHandCursor); }
protected:
    bool hitButton(const QPoint &position) const override { return rect().contains(position); }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen); painter.setBrush(isChecked() ? QColor("#e07b20") : QColor("#d9c3ab"));
        painter.drawRoundedRect(QRectF(0,0,40,22),11,11);
        painter.setBrush(QColor(0,0,0,25)); painter.drawEllipse(QRectF(isChecked()?21:3,4,16,16));
        painter.setBrush(Qt::white); painter.drawEllipse(QRectF(isChecked()?21:3,3,16,16));
        if (hasFocus()) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(QColor("#8b5e34"),1,Qt::DotLine)); painter.drawRoundedRect(rect().adjusted(1,1,-1,-1),10,10); }
    }
};

class ModeCombo final : public QComboBox {
public:
    using QComboBox::QComboBox;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        if (!isEnabled()) painter.setOpacity(.4);
        painter.setBrush(Qt::white); painter.setPen(QPen(QColor("#f4a460"),1));
        painter.drawRoundedRect(QRectF(.5,.5,width()-1,height()-1),8,8);
        painter.setPen(QColor("#3d2200"));
        painter.drawText(QRect(10,0,width()-28,height()),Qt::AlignVCenter|Qt::AlignLeft,currentText());
        painter.setPen(QPen(QColor("#767676"),1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        painter.drawPolyline(QPolygonF{QPointF(width()-14,height()/2.-2),QPointF(width()-10,height()/2.+2),QPointF(width()-6,height()/2.-2)});
    }
};

class SourceChip final : public QPushButton {
public:
    SourceChip(QString title, QColor color, int count, bool active, bool error)
        : title_(std::move(title)), color_(color), count_(count), active_(active), error_(error) {
        setText(title_); setCheckable(true); setChecked(active); setCursor(Qt::PointingHandCursor); setAutoDefault(false);
        QFont font; font.setPixelSize(13); font.setBold(true); setFont(font);
        setFixedSize(sizeHint());
    }
    QSize sizeHint() const override {
        return QSize(24+fontMetrics().horizontalAdvance(title_)+(active_?0:13)+(count_>0?badgeWidth()+6:0)+(error_?21:0),31);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setPen(QPen(color_,1.5));
        p.setBrush(active_?color_:QColor(255,255,255,217)); p.drawRoundedRect(QRectF(.75,.75,width()-1.5,height()-1.5),15,15);
        int x=12;
        if(!active_){p.setPen(Qt::NoPen);p.setBrush(color_);p.drawEllipse(QRectF(x,12,7,7));x+=13;}
        p.setFont(font());p.setPen(active_?Qt::white:QColor("#6b3a00"));
        const int titleWidth=fontMetrics().horizontalAdvance(title_);p.drawText(QRect(x,0,titleWidth,31),Qt::AlignCenter,title_);x+=titleWidth;
        if(count_>0){x+=6;const int w=badgeWidth();p.setPen(Qt::NoPen);p.setBrush(active_?QColor(255,255,255,82):QColor(61,34,0,36));p.drawRoundedRect(QRectF(x,6.5,w,18),9,9);QFont f=font();f.setPixelSize(11);p.setFont(f);p.setPen(active_?Qt::white:QColor("#6b3a00"));p.drawText(QRectF(x,6.5,w,18),Qt::AlignCenter,count_>99?QStringLiteral("99+"):QString::number(count_));x+=w;}
        if(error_){x+=6;p.setPen(Qt::NoPen);p.setBrush(QColor("#dc2626"));p.drawEllipse(QRectF(x,8,15,15));p.setPen(Qt::white);p.drawText(QRect(x,8,15,15),Qt::AlignCenter,QStringLiteral("!"));}
        if(hasFocus()){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#e07b20"),1,Qt::DotLine));p.drawRoundedRect(rect().adjusted(2,2,-2,-2),13,13);}
    }
private:
    int badgeWidth() const {QFont f=font();f.setPixelSize(11);return std::max(18,QFontMetrics(f).horizontalAdvance(count_>99?QStringLiteral("99+"):QString::number(count_))+10);}
    QString title_; QColor color_; int count_; bool active_; bool error_;
};

QLabel *label(const QString &text, QWidget *parent = nullptr, const QString &name = {}) {
    auto *result = new QLabel(text, parent);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    if (!name.isEmpty()) result->setObjectName(name);
    return result;
}

class DialogBackground final : public QObject {
public:
    DialogBackground(QDialog *dialog,QWidget *document) : QObject(dialog),dialog_(dialog),document_(document) {
        dialog->installEventFilter(this);
        if(document)document->installEventFilter(this);
        QTimer::singleShot(0,this,[this]{update();});
    }
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::Resize || event->type()==QEvent::LayoutRequest)
            QTimer::singleShot(0,this,[this]{update();});
        return false;
    }
private:
    void update() {
        const int width=document_?document_->width():dialog_->width();
        int height=dialog_->height();
        if(document_&&document_->layout()) {
            auto *layout=document_->layout();
            if(layout->count()>0)height=layout->itemAt(layout->count()-1)->geometry().bottom()+1+layout->contentsMargins().bottom();
        }
        else if(dialog_->windowTitle()==QStringLiteral("计时规则说明")) height=461;
        height=std::max(1,height);
        if(width==lastSize_.width()&&height==lastSize_.height())return;
        lastSize_=QSize(width,height);
        const qreal dpr=dialog_->devicePixelRatioF();
        QPixmap tile(qRound(width*dpr),qRound(height*dpr));tile.setDevicePixelRatio(dpr);
        QPainter painter(&tile);
        // CSS linear-gradient(160deg): the gradient line crosses the center
        // and projects all four corners onto the direction vector.
        const QPointF direction(.3420201433256687,.9396926207859084);
        const qreal length=width*direction.x()+height*direction.y();
        const QPointF center(width/2.,height/2.);
        QLinearGradient gradient(center-direction*(length/2),center+direction*(length/2));
        gradient.setColorAt(0,QColor("#f4a460"));gradient.setColorAt(1,QColor("#ff8c42"));
        painter.fillRect(QRect(0,0,width,height),gradient);painter.end();
        auto palette=dialog_->palette();palette.setBrush(QPalette::Window,QBrush(tile));dialog_->setPalette(palette);dialog_->setAutoFillBackground(true);
    }
    QDialog *dialog_; QWidget *document_; QSize lastSize_;
};

QVBoxLayout *prepare(QDialog *dialog, const QString &title, int width, int height) {
    dialog->setObjectName(QStringLiteral("purrDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
#ifdef Q_OS_LINUX
    if (QFontDatabase::families().contains(QStringLiteral("Droid Sans Fallback"))) {
        auto font=dialog->font();
        auto families=font.families(); families.append(QStringLiteral("Droid Sans Fallback"));
        font.setFamilies(families); dialog->setFont(font);
    }
#endif
    dialog->setStyleSheet(QString::fromLatin1(dialogStyle));
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    const bool document = title != QStringLiteral("待办消息") && title != QStringLiteral("计时规则说明");
    auto *outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *layout = outer;
    QWidget *documentWidget = nullptr;
    if (document) {
        auto *scroll = new QScrollArea(dialog);
        scroll->setObjectName(QStringLiteral("documentScroll"));
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *body = new QWidget;
        body->setObjectName(QStringLiteral("documentBody"));
        documentWidget = body;
        layout = new QVBoxLayout(body);
        layout->setAlignment(Qt::AlignTop);
        scroll->setWidget(body);
        outer->addWidget(scroll);
    }
    const bool messages = title == QStringLiteral("消息提醒设置");
    layout->setContentsMargins(messages ? 22 : 24, messages ? 22 : 24, messages ? 22 : 24, messages ? 28 : 24);
    layout->setSpacing(16);
    auto *heading = label(title, dialog, QStringLiteral("heading"));
    heading->setFixedHeight(28);
    layout->addWidget(heading);
    const auto *screen = qApp->primaryScreen();
    const auto available = screen ? screen->availableGeometry() : QRect(0, 0, 1024, 768);
    dialog->resize(std::min(width, available.width() - 32), std::min(height, qRound(available.height() * .8)));
    dialog->move(available.center() - dialog->rect().center());
    new DialogBackground(dialog,documentWidget);
    return layout;
}

QVBoxLayout *panel(QVBoxLayout *parent) {
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("panel"));
    frame->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(0);
    auto *shadow = new QGraphicsDropShadowEffect(frame);
    shadow->setBlurRadius(8); shadow->setOffset(0, 2); shadow->setColor(QColor(0, 0, 0, 26));
    frame->setGraphicsEffect(shadow);
    parent->addWidget(frame);
    return layout;
}

QVBoxLayout *scrollBodyLayout(QVBoxLayout *parent) {
    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    parent->addLayout(layout);
    return layout;
}

QPushButton *button(const QString &text, bool primary = false) {
    auto *result = new QPushButton(text);
    if (primary) result->setObjectName(QStringLiteral("primary"));
    result->setCursor(Qt::PointingHandCursor);
    result->setAutoDefault(false);
    return result;
}

QVBoxLayout *collapsible(QVBoxLayout *layout, const QString &title, const QString &objectName, const QString &hint = {}) {
    auto *header = new QHBoxLayout;
    auto *toggle = button(QStringLiteral("▸ ") + title);
    toggle->setObjectName(objectName);
    toggle->setCheckable(true);
    toggle->setAccessibleName(title);
    toggle->setStyleSheet(QStringLiteral("QPushButton {background:transparent;color:#8b5e34;border:0;text-align:left;padding:4px 0;} QPushButton:hover {color:#e07b20;} QPushButton:focus {border:1px solid #e07b20;padding:3px 0;}"));
    toggle->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed);
    header->addWidget(toggle);
    if (!hint.isEmpty()) { auto *description=label(hint,nullptr,QStringLiteral("hint")); description->setWordWrap(false); header->addWidget(description,1,Qt::AlignRight); }
    else header->addStretch();
    layout->addLayout(header);
    auto *content = new QWidget;
    content->setObjectName(objectName + QStringLiteral("Body"));
    auto *body = new QVBoxLayout(content);
    body->setContentsMargins(0, 4, 0, 0);
    body->setSpacing(9);
    layout->addWidget(content);
    content->hide();
    QObject::connect(toggle, &QPushButton::toggled, content, [toggle, content, title](bool expanded) {
        toggle->setText((expanded ? QStringLiteral("▾ ") : QStringLiteral("▸ ")) + title);
        content->setVisible(expanded);
    });
    return body;
}

QPushButton *actionButtons(QVBoxLayout *layout, QDialog *dialog, const QString &saveText = QStringLiteral("保存")) {
    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    row->addStretch();
    auto *cancel = button(QStringLiteral("取消"));
    cancel->setObjectName(QStringLiteral("cancelButton"));
    cancel->setStyleSheet(QStringLiteral("background:rgba(255,255,255,128);color:#6b3a00;border:0;padding:8px 24px;font-weight:400;"));
    cancel->setFixedHeight(37);
    row->addWidget(cancel);
    auto *save = button(saveText, true);
    save->setStyleSheet(QStringLiteral("border:0;padding:8px 24px;font-weight:700;"));
    save->setFixedHeight(37);
    row->addWidget(save);
    layout->addLayout(row);
    QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    return save;
}

void field(QVBoxLayout *layout, const QString &title, QWidget *control, const QString &hint = {}) {
    const bool first = !layout->property("hasFields").toBool();
    layout->setProperty("hasFields", true);
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("fieldRow"));
    frame->setStyleSheet(first ? QString() : QStringLiteral("QFrame#fieldRow {border-top:1px solid rgba(244,164,96,51);}"));
    auto *row = new QHBoxLayout(frame);
    row->setContentsMargins(0, 10, 0, 10);
    row->setSpacing(14);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *name = label(title);
    name->setStyleSheet(QStringLiteral("font-size:14px;font-weight:500;"));
    name->setFixedHeight(20);
    texts->addWidget(name);
    if (!hint.isEmpty()) {
        auto *description = label(hint, nullptr, QStringLiteral("hint"));
        description->setMinimumHeight(18);
        description->setWordWrap(true);
        description->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        texts->addWidget(description);
    }
    row->addLayout(texts, 1);
    row->addWidget(control, 0, Qt::AlignVCenter);
    frame->setMinimumHeight(first ? 60 : 61);
    frame->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    layout->addWidget(frame);
}

QWidget *numberControl(QSpinBox *spin, const QString &unit) {
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setAlignment(Qt::AlignCenter);
    spin->setFixedSize(70, 32);
    spin->setStyleSheet(QStringLiteral("padding:6px 10px;font-family:Arial;font-size:%1pt;border-radius:8px;").arg(15.2 * 72 / spin->logicalDpiY()));
    auto *container = new QWidget;
    auto *row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0); row->setSpacing(5);
    row->addWidget(spin);
    auto *suffix = label(unit); suffix->setStyleSheet(QStringLiteral("font-size:16px;"));
    row->addWidget(suffix);
    return container;
}

QWidget *checkboxControl(QCheckBox *checkbox) {
    // Inline HTML checkboxes sit on the text baseline, two pixels above the
    // visual center of their field row.
    auto *container = new QWidget;
    container->setFixedSize(18, 22);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->addWidget(checkbox);
    return container;
}

QLineEdit *directoryPicker(QVBoxLayout *layout, const QString &title, const QString &directory, bool enabled = true) {
    auto *controls = new QWidget;
    auto *buttons = new QHBoxLayout(controls);
    buttons->setContentsMargins(0,0,0,0); buttons->setSpacing(6);
    auto *pick = button(QStringLiteral("选择"));
    auto *reset = button(QStringLiteral("重置"));
    pick->setStyleSheet(QStringLiteral("padding:4px 12px;border:1px solid #f4a460;border-radius:6px;font-size:13px;color:#8b5e34;font-weight:400;"));
    reset->setStyleSheet(QStringLiteral("padding:4px 12px;border:1px solid #ddd;border-radius:6px;font-size:13px;color:#999;font-weight:400;"));
    buttons->addWidget(pick); buttons->addWidget(reset);
    pick->setEnabled(enabled); reset->setEnabled(enabled);
    if(!enabled){for(auto *control:{pick,reset}){auto *opacity=new QGraphicsOpacityEffect(control);opacity->setOpacity(.4);control->setGraphicsEffect(opacity);}}
    field(layout, title, controls, enabled ? (directory.isEmpty() ? QStringLiteral("使用默认目录") : directory) : QStringLiteral("激活后可自定义素材目录"));
    auto *frame = layout->itemAt(layout->count()-1)->widget();
    auto *hint = frame->findChild<QLabel *>(QStringLiteral("hint"));
    hint->setToolTip(directory); hint->setWordWrap(false); hint->setFixedHeight(18); hint->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    if (!enabled) hint->setStyleSheet(QStringLiteral("font-size:12px;color:#999;"));
    auto *path = new QLineEdit(directory, controls);
    path->hide();
    QObject::connect(path, &QLineEdit::textChanged, hint, [hint](const QString &text) {
        hint->setText(text.isEmpty() ? QStringLiteral("使用默认目录") : text); hint->setToolTip(text);
    });
    QObject::connect(pick, &QPushButton::clicked, path, [path] {
        const auto selected = QFileDialog::getExistingDirectory(path->window(), QStringLiteral("选择素材目录"), path->text());
        if (!selected.isEmpty()) path->setText(selected);
    });
    QObject::connect(reset, &QPushButton::clicked, path, &QLineEdit::clear);
    return path;
}

QString safeColor(const QString &color) {
    return QRegularExpression(QStringLiteral("^#[0-9a-fA-F]{6}$")).match(color).hasMatch() ? color : QStringLiteral("#8b5e34");
}

QIcon colorIcon(const QString &color) {
    QPixmap image(12, 12);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(safeColor(color)));
    painter.drawEllipse(QRectF(1, 1, 10, 10));
    return QIcon(image);
}

void setInvalid(QWidget *control, bool invalid) {
    if (!control->property("normalStyle").isValid()) control->setProperty("normalStyle", control->styleSheet());
    control->setProperty("invalid", invalid);
    control->setStyleSheet(control->property("normalStyle").toString() + (invalid ? QStringLiteral("border:1px solid #dc2626;") : QString()));
}

bool validHttpUrl(const QString &value) {
    const QUrl url(value, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty() && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"));
}

bool validHeaders(QString value) {
    if (value.trimmed().isEmpty()) value = QStringLiteral("{}");
    return QJsonDocument::fromJson(value.toUtf8()).isObject();
}

void addSaveStatus(QDialog *dialog, QVBoxLayout *layout, QLabel **status, QTimer **timer) {
    *status = label({}, dialog, QStringLiteral("saveStatus"));
    (*status)->hide();
    Q_UNUSED(layout);
    (*status)->setAttribute(Qt::WA_TransparentForMouseEvents);
    (*status)->setAlignment(Qt::AlignCenter);
    *timer = new QTimer(dialog);
    (*timer)->setSingleShot(true);
    (*timer)->setInterval(2500);
    QObject::connect(*timer, &QTimer::timeout, *status, &QWidget::hide);
}

void showSaveStatus(QLabel *status, QTimer *timer, bool ok, const QString &error) {
    status->setText(ok ? QStringLiteral("保存成功 ✓") : error.isEmpty() ? QStringLiteral("保存失败") : error);
    status->setStyleSheet(ok ? QStringLiteral("background:#ecfdf3;color:#16a34a;border-radius:8px;padding:10px;")
                            : QStringLiteral("background:#fef2f2;color:#dc2626;border-radius:8px;padding:10px;"));
    status->setMaximumWidth(std::max(120, status->parentWidget()->width() - 32));
    status->adjustSize();
    status->move((status->parentWidget()->width() - status->width()) / 2, status->parentWidget()->height() - status->height() - 16);
    status->show(); status->raise();
    timer->start();
}
} // namespace

SettingsDialog::SettingsDialog(QJsonObject config, bool activated, bool autostart, QWidget *parent)
    : QDialog(parent) {
    auto *root = prepare(this, QStringLiteral("胖猫暂停一下（PurrPause） 设置"), 400, 560);
    auto *body = scrollBodyLayout(root);
    auto *content = panel(body);
    auto *threshold = new QSpinBox;
    threshold->setObjectName(QStringLiteral("thresholdMinutes"));
    threshold->setRange(1, 480); threshold->setValue(config.value(QStringLiteral("thresholdMinutes")).toInt(45));
    field(content, QStringLiteral("屏幕使用时间"), numberControl(threshold, QStringLiteral("分钟")), QStringLiteral("连续使用多久后提醒休息"));
    auto *duration = new QSpinBox;
    duration->setObjectName(QStringLiteral("breakMinutes"));
    duration->setRange(1, 60); duration->setValue(config.value(QStringLiteral("breakMinutes")).toInt(5));
    field(content, QStringLiteral("休息时长"), numberControl(duration, QStringLiteral("分钟")), QStringLiteral("每次休息持续多久"));
    auto *mode = new ModeCombo;
    mode->setObjectName(QStringLiteral("animationMode"));
    mode->addItem(QStringLiteral("右下角走出"), QStringLiteral("walk-zoom"));
    mode->addItem(QStringLiteral("中央淡入"), QStringLiteral("fade-center"));
    mode->addItem(QStringLiteral("右侧走出"), QStringLiteral("walk-flat"));
    mode->addItem(QStringLiteral("中央走出"), QStringLiteral("walk-center"));
    mode->setCurrentIndex(std::max(0, mode->findData(config.value(QStringLiteral("animationMode")).toString(QStringLiteral("walk-center")))));
    mode->setEnabled(activated);
    mode->setFixedSize(114, 32);
    mode->setStyleSheet(QStringLiteral("padding:6px 10px;border-radius:8px;font-family:Arial;font-size:%1pt;").arg(14.4 * 72 / mode->logicalDpiY()));
    field(content, QStringLiteral("动画模式"), mode, QStringLiteral("猫咪出现的方式"));
    auto *directory = directoryPicker(content, QStringLiteral("素材目录"), config.value(QStringLiteral("customWebmDir")).toString(), activated);
    directory->setObjectName(QStringLiteral("customWebmDir"));
    auto *launch = new CheckBox;
    launch->setObjectName(QStringLiteral("autoLaunch"));
    launch->setChecked(autostart);
    field(content, QStringLiteral("开机自动启动"), checkboxControl(launch), QStringLiteral("系统启动时自动运行 胖猫暂停一下（PurrPause）"));
    auto *logging = new CheckBox;
    logging->setObjectName(QStringLiteral("logEnabled"));
    logging->setChecked(config.value(QStringLiteral("logEnabled")).toBool());
    field(content, QStringLiteral("运行日志"), checkboxControl(logging), QStringLiteral("记录运行信息到日志文件，用于排查问题（默认关闭）"));
    addSaveStatus(this, root, &saveStatus_, &saveStatusTimer_);
    auto *save = actionButtons(root, this);
    connect(save, &QPushButton::clicked, this, [this, threshold, duration, mode, directory, launch, logging] {
        emit saved({{QStringLiteral("thresholdMinutes"), threshold->value()}, {QStringLiteral("breakMinutes"), duration->value()},
            {QStringLiteral("animationMode"), mode->currentData().toString()}, {QStringLiteral("customWebmDir"), directory->text()},
            {QStringLiteral("autoLaunch"), launch->isChecked()}, {QStringLiteral("logEnabled"), logging->isChecked()}});
    });
}

void SettingsDialog::showSaveResult(bool ok, QString error) {
    showSaveStatus(saveStatus_, saveStatusTimer_, ok, error);
}

ActivationDialog::ActivationDialog(QJsonObject status, QString machineId, QWidget *parent)
    : QDialog(parent) {
    auto *root = prepare(this, QStringLiteral("激活 胖猫暂停一下（PurrPause）"), 540, 540);
    auto *body = scrollBodyLayout(root);
    auto *content = panel(body);
    auto *statusRow=new QWidget;
    field(content,QStringLiteral("授权状态"),statusRow,QStringLiteral("检查中..."));
    status_ = content->itemAt(0)->widget()->findChild<QLabel *>(QStringLiteral("hint"));
    content->itemAt(0)->widget()->layout()->setContentsMargins(0, 11, 0, 9);
    status_->setObjectName(QStringLiteral("licenseStatus"));
    status_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    status_->setMinimumHeight(0);
    status_->setFixedHeight(17);
    auto *machineControls=new QWidget;
    auto *machineRow = new QHBoxLayout(machineControls); machineRow->setContentsMargins(0,0,0,0); machineRow->setSpacing(6);
    auto *machine = new QLineEdit(machineId); machine->setObjectName(QStringLiteral("machineId")); machine->setReadOnly(true);
    machine->setStyleSheet(QStringLiteral("font-family:monospace;font-size:%1pt;background:#f9f0e6;border:0;border-radius:4px;padding:3px 8px;").arg(13.6 * 72 / machine->logicalDpiY()));
    auto machineFont = machine->font();
    machineFont.setFamily(QStringLiteral("monospace"));
    machineFont.setPointSizeF(13.6 * 72 / machine->logicalDpiY());
    machineFont.setLetterSpacing(QFont::AbsoluteSpacing, .5);
    // Qt rounds the raster font to whole pixels; retain the CSS advance width
    // when the requested 13.6 px monospace face becomes a 14 px face.
    machineFont.setStretch(qRound(100 * 13.6 / QFontInfo(machineFont).pixelSize()));
    machine->setFont(machineFont);
    machine->setTextMargins(-2, 0, -2, 0);
    machine->setFixedHeight(22);
    machine->setFixedWidth(qRound(QFontMetricsF(machineFont).horizontalAdvance(machineId) + 16));
    auto *copy = button(QStringLiteral("复制"));
    copy->setStyleSheet(QStringLiteral("padding:4px 12px;border:1px solid #f4a460;border-radius:6px;font-size:13px;font-weight:400;color:#8b5e34;"));
    auto *machineBaseline = new QWidget;
    auto *machineBaselineLayout = new QVBoxLayout(machineBaseline);
    machineBaselineLayout->setContentsMargins(0, 2, 0, 0);
    machineBaselineLayout->addWidget(machine);
    machineRow->addWidget(machineBaseline); machineRow->addWidget(copy);
    field(content,QStringLiteral("机器码"),machineControls,QStringLiteral("将此机器码发送给开发者获取序列号"));
    connect(copy, &QPushButton::clicked, this, [copy, machineId] {
        QApplication::clipboard()->setText(machineId); copy->setText(QStringLiteral("已复制"));
        QTimer::singleShot(1500,copy,[copy]{copy->setText(QStringLiteral("复制"));});
    });
    auto *serialFrame=new QFrame; serialFrame->setObjectName(QStringLiteral("serialField"));
    serialFrame->setStyleSheet(QStringLiteral("QFrame#serialField{border-top:1px solid rgba(244,164,96,51);}"));
    serialFrame->setFixedHeight(134);
    auto *serialLayout=new QVBoxLayout(serialFrame); serialLayout->setAlignment(Qt::AlignTop); serialLayout->setContentsMargins(0,10,0,10); serialLayout->setSpacing(8);
    auto *serialTitle=label(QStringLiteral("序列号")); serialTitle->setFixedHeight(21); serialLayout->addWidget(serialTitle);
    auto *serial = new QPlainTextEdit; serial->setObjectName(QStringLiteral("serialKey"));
    serial->setPlaceholderText(QStringLiteral("粘贴序列号（PP-开头）")); serial->setFixedHeight(80); serial->setTabChangesFocus(true);
    serial->setStyleSheet(QStringLiteral("font-family:monospace;font-size:%1pt;padding:8px 12px;border-radius:8px;").arg(12.8 * 72 / serial->logicalDpiY()));
    auto serialPalette=serial->palette(); serialPalette.setColor(QPalette::PlaceholderText,QColor("#cba070")); serial->setPalette(serialPalette);
    serialLayout->addWidget(serial); content->addWidget(serialFrame);
    error_ = label({},this,QStringLiteral("activationError")); error_->hide(); error_->setAlignment(Qt::AlignCenter);
    error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    later_ = button(QStringLiteral("稍后再说"));
    later_->setStyleSheet(QStringLiteral("background:rgba(255,255,255,128);color:#6b3a00;border:0;padding:8px 24px;font-weight:400;"));
    buttons->setSpacing(10);
    later_->setFixedHeight(37);
    buttons->addWidget(later_);
    connect(later_, &QPushButton::clicked, this, &QDialog::reject);
    auto *activate = button(QStringLiteral("激活"), true);
    activate->setStyleSheet(QStringLiteral("QPushButton{border:0;padding:8px 24px;font-weight:700;} QPushButton:disabled{background:#f0f0f0;color:#bbb;}"));
    activate->setFixedHeight(37);
    activate->setEnabled(false);
    buttons->addWidget(activate);
    root->addLayout(buttons);
    connect(serial, &QPlainTextEdit::textChanged, this, [serial, activate] { activate->setEnabled(serial->toPlainText().trimmed().size() >= 20); });
    connect(activate, &QPushButton::clicked, this, [this, serial] {
        error_->hide();
        emit activationRequested(serial->toPlainText().trimmed());
    });
    setStatus(status);
}

void ActivationDialog::setStatus(QJsonObject status) {
    const auto value = status.value(QStringLiteral("status")).toString();
    if (value == QStringLiteral("active")) {
        status_->setText(status.value(QStringLiteral("type")).toString() == QStringLiteral("permanent")
            ? QStringLiteral("已激活（永久）") : QStringLiteral("已激活（剩余 %1 天）").arg(status.value(QStringLiteral("daysLeft")).toInt()));
        later_->setText(QStringLiteral("稍后再说"));
    } else if (value == QStringLiteral("trial")) {
        status_->setText(QStringLiteral("试用中（剩余 %1 天）").arg(status.value(QStringLiteral("daysLeft")).toInt()));
        later_->setText(QStringLiteral("稍后再说"));
    } else status_->setText(status.value(QStringLiteral("type")).toString() == QStringLiteral("trial") ? QStringLiteral("试用已到期") : QStringLiteral("授权已到期"));
    status_->setStyleSheet(value == QStringLiteral("expired") ? QStringLiteral("font-size:12px;color:#e74c3c;") : QStringLiteral("font-size:12px;color:#8b5e34;"));
    later_->setVisible(value != QStringLiteral("expired"));
}

void ActivationDialog::showError(QString error) {
    error_->setText(error); error_->setStyleSheet(QStringLiteral("font-size:14px;color:#dc2626;background:rgba(239,68,68,38);border-radius:8px;padding:8px 24px;"));
    error_->setMaximumWidth(width()-32); error_->adjustSize(); error_->move((width()-error_->width())/2,height()-error_->height()-16); error_->show(); error_->raise();
}

void ActivationDialog::showSuccess(QJsonObject status) {
    setStatus(status);
    const QString detail=status.value(QStringLiteral("type")).toString()==QStringLiteral("permanent")
        ? QStringLiteral("永久授权") : QStringLiteral("有效期至 ")+status.value(QStringLiteral("expiresAt")).toString().section(QLatin1Char('T'),0,0);
    error_->setText(QStringLiteral("激活成功！（%1）").arg(detail));
    error_->setStyleSheet(QStringLiteral("font-size:14px;color:#16a34a;background:rgba(34,197,94,38);border-radius:8px;padding:8px 24px;"));
    error_->setMaximumWidth(width()-32); error_->adjustSize(); error_->move((width()-error_->width())/2,height()-error_->height()-16); error_->show(); error_->raise();
}

class SourceEditor final : public QFrame {
public:
    SourceEditor(QJsonObject source, QWidget *parent = nullptr) : QFrame(parent) {
        setObjectName(QStringLiteral("sourceCard"));
        setStyleSheet(QStringLiteral("QFrame#sourceCard{background:rgba(255,255,255,235);border:1px solid rgba(224,123,32,71);border-radius:12px;}"));
        id = source.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) id = QStringLiteral("src-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(16,13,16,14); layout->setSpacing(0);
        auto *top = new QHBoxLayout; top->setSpacing(10);
        name = new QLineEdit(source.value(QStringLiteral("name")).toString());
        name->setObjectName(QStringLiteral("sourceName")); name->setPlaceholderText(QStringLiteral("接口源名称"));
        name->setAccessibleName(QStringLiteral("接口源名称")); name->setFixedHeight(34);
        name->setStyleSheet(QStringLiteral("padding:7px 11px;border-radius:8px;font-family:Arial;font-size:%1pt;font-weight:600;").arg(15.2 * 72 / name->logicalDpiY()));
        enabled = new Switch; enabled->setAccessibleName(QStringLiteral("启用接口源"));
        enabled->setChecked(source.value(QStringLiteral("enabled")).toBool(true));
        remove = button(QStringLiteral("×")); remove->setAccessibleName(QStringLiteral("删除该接口源"));
        remove->setToolTip(QStringLiteral("删除该接口源")); remove->setFixedSize(28,28);
        remove->setStyleSheet(QStringLiteral("padding:0;border:1px solid #ddd;border-radius:8px;color:#c0392b;font-size:16px;font-weight:400;"));
        top->addWidget(name,1); top->addWidget(enabled); top->addWidget(remove); layout->addLayout(top);
        layout->addSpacing(11);
        auto *addressLabel=label(QStringLiteral("待办接口地址（完整 URL）"));
        addressLabel->setStyleSheet(QStringLiteral("font-size:13px;color:#8b5e34;")); addressLabel->setFixedHeight(19);
        layout->addWidget(addressLabel); layout->addSpacing(5);
        url = new QLineEdit(source.value(QStringLiteral("listUrl")).toString());
        url->setObjectName(QStringLiteral("sourceUrl")); url->setPlaceholderText(QStringLiteral("https://oa.example.com/purr-pause/v1/todos"));
        url->setAccessibleName(QStringLiteral("待办接口地址")); url->setFixedHeight(32);
        url->setStyleSheet(QStringLiteral("padding:7px 11px;border-radius:8px;font-family:Arial;font-size:%1pt;").arg(14.4 * 72 / url->logicalDpiY())); layout->addWidget(url);
        layout->addSpacing(5);
        auto *hint=label(QStringLiteral("完整的待办列表接口地址，PurrPause 只在其后追加 limit/offset"));
        hint->setStyleSheet(QStringLiteral("font-size:11px;color:#b98a5d;")); hint->setFixedHeight(17); layout->addWidget(hint);
        auto *example=new QHBoxLayout; example->setContentsMargins(0,3,0,0); example->setSpacing(5);
        auto *prefix=label(QStringLiteral("推荐")); prefix->setStyleSheet(QStringLiteral("font-size:11px;color:#a98563;"));
        auto *code=label(QStringLiteral("https://你的域名/purr-pause/v1/todos"));
        code->setStyleSheet(QStringLiteral("font-family:monospace;font-size:11px;color:#c47f33;background:rgba(224,123,32,26);padding:1px 6px;border-radius:5px;"));
        code->setWordWrap(false); code->setFixedHeight(16);
        example->addWidget(prefix); example->addWidget(code); example->addStretch(); layout->addLayout(example);
        layout->addSpacing(12);
        auto *headersBody=collapsible(layout,QStringLiteral("请求头（可选）"),QStringLiteral("sourceHeadersToggle"));
        findChild<QPushButton *>(QStringLiteral("sourceHeadersToggle"))->setStyleSheet(QStringLiteral("background:transparent;border:0;text-align:left;color:#8b5e34;font-size:13px;font-weight:400;padding:0;"));
        headersBody->addWidget(label(QStringLiteral("JSON 对象，例如 {\"Authorization\":\"Bearer xxx\"}"),this,QStringLiteral("hint")));
        headers = new QPlainTextEdit(source.value(QStringLiteral("authHeaders")).toString(QStringLiteral("{}")));
        headers->setObjectName(QStringLiteral("sourceHeaders")); headers->setAccessibleName(QStringLiteral("请求头 JSON"));
        headers->setPlaceholderText(QStringLiteral("{\"Authorization\":\"Bearer xxx\"}"));
        headers->setFixedHeight(64); headers->setTabChangesFocus(true);
        headers->setStyleSheet(QStringLiteral("font-family:monospace;font-size:13px;padding:8px 10px;")); headersBody->addWidget(headers);
        layout->addSpacing(12);
        auto *testFrame=new QFrame; testFrame->setObjectName(QStringLiteral("sourceTestRow"));
        testFrame->setStyleSheet(QStringLiteral("QFrame#sourceTestRow{border-top:1px solid rgba(244,164,96,64);}"));
        auto *bottom = new QHBoxLayout(testFrame); bottom->setContentsMargins(0,11,0,0); bottom->setSpacing(10);
        result = label({},this,QStringLiteral("sourceTestResult")); result->setStyleSheet(QStringLiteral("font-size:13px;color:#8b5e34;"));
        result->setTextInteractionFlags(Qt::TextSelectableByMouse);
        test = button(QStringLiteral("测试连接")); test->setObjectName(QStringLiteral("sourceTest"));
        test->setStyleSheet(QStringLiteral("padding:5px 14px;border-radius:8px;font-size:13px;")); test->setFixedHeight(32);
        bottom->addWidget(test); bottom->addWidget(result,1); layout->addWidget(testFrame);
        auto *opacity=new QGraphicsOpacityEffect(this); opacity->setOpacity(enabled->isChecked()?1:.62); setGraphicsEffect(opacity);
        connect(enabled,&QCheckBox::toggled,this,[opacity](bool checked){opacity->setOpacity(checked?1:.62);});
        connect(name,&QLineEdit::textChanged,this,[this]{setInvalid(name,false);});
        connect(url,&QLineEdit::textChanged,this,[this]{setInvalid(url,false);});
        connect(headers,&QPlainTextEdit::textChanged,this,[this]{setInvalid(headers,false);});
    }
    void expandHeaders() { findChild<QPushButton *>(QStringLiteral("sourceHeadersToggle"))->setChecked(true); }
    void testResult(const QString &text, bool ok = false) {
        result->setText(text);
        result->setStyleSheet(ok ? QStringLiteral("color:#16a34a;") : QStringLiteral("color:#dc2626;"));
    }
    QJsonObject value() const {
        return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name->text().trimmed()},
            {QStringLiteral("listUrl"), url->text().trimmed()}, {QStringLiteral("enabled"), enabled->isChecked()},
            {QStringLiteral("authHeaders"), headers->toPlainText().trimmed().isEmpty() ? QStringLiteral("{}") : headers->toPlainText().trimmed()}};
    }
    QString id;
    QCheckBox *enabled;
    QLineEdit *name;
    QLineEdit *url;
    QPlainTextEdit *headers;
    QPushButton *remove;
    QPushButton *test;
    QLabel *result;
};

MessageSettingsDialog::MessageSettingsDialog(QJsonObject config, QWidget *parent)
    : QDialog(parent) {
    auto *root = prepare(this, QStringLiteral("消息提醒设置"), 540, 660);
    connectionTimer_ = new QTimer(this);
    connectionTimer_->setSingleShot(true);
    connectionTimer_->setInterval(15000);
    connect(connectionTimer_, &QTimer::timeout, this, [this] {
        if (!activeTest_) return;
        auto *editor = activeTest_;
        finishConnectionTest();
        editor->testResult(QStringLiteral("✗ 测试超时（主进程无响应）"));
    });
    root->setSpacing(0);
    root->addSpacing(6);
    auto *subtitle=label(QStringLiteral("配置一个或多个第三方接口源，胖猫会轮询待办并用小火箭提醒你。"));
    subtitle->setStyleSheet(QStringLiteral("font-size:12px;color:rgba(255,255,255,224);")); subtitle->setFixedHeight(18);
    root->addWidget(subtitle); root->addSpacing(16);
    auto *body = scrollBodyLayout(root);
    auto *overview = panel(body); overview->setContentsMargins(18,14,18,14);
    auto *master=new QHBoxLayout; master->setSpacing(14);
    auto *masterText=new QVBoxLayout; masterText->setSpacing(2);
    auto *masterTitle=label(QStringLiteral("启用消息提醒")); masterTitle->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;"));
    masterTitle->setFixedHeight(23); masterText->addWidget(masterTitle);
    auto *masterHint=label(QStringLiteral("关闭后停止所有接口源的轮询并隐藏待办角标"),nullptr,QStringLiteral("hint")); masterHint->setFixedHeight(18);
    masterText->addWidget(masterHint); master->addLayout(masterText,1);
    auto *enabled = new Switch;
    enabled->setObjectName(QStringLiteral("messageNotifyEnabled")); enabled->setAccessibleName(QStringLiteral("启用消息提醒"));
    enabled->setChecked(config.value(QStringLiteral("messageNotifyEnabled")).toBool());
    master->addWidget(enabled); overview->addLayout(master);
    body->addSpacing(18);
    auto *sourcesHeader = new QHBoxLayout; sourcesHeader->setContentsMargins(2,0,2,0); sourcesHeader->setSpacing(9);
    auto *sourcesTitle=label(QStringLiteral("接口源")); sourcesTitle->setStyleSheet(QStringLiteral("font-size:14px;font-weight:700;color:white;"));
    sourcesHeader->addWidget(sourcesTitle);
    sourceCount_=label(QStringLiteral("0")); sourceCount_->setObjectName(QStringLiteral("sourceCount"));
    sourceCount_->setFixedHeight(16); sourceCount_->setMaximumWidth(25);
    sourceCount_->setStyleSheet(QStringLiteral("font-size:%1pt;font-weight:500;color:white;background:rgba(255,255,255,71);border-radius:9px;padding:1px 9px;").arg(11.52 * 72 / sourceCount_->logicalDpiY()));
    sourcesHeader->addWidget(sourceCount_); sourcesHeader->addStretch();
    auto *exportSpec = button(QStringLiteral("▯ 下载接口规范")); exportSpec->setFixedHeight(26);
    exportSpec->setStyleSheet(QStringLiteral("padding:3px 12px;border:1px solid rgba(255,255,255,140);border-radius:13px;background:rgba(61,34,0,140);color:white;font-size:12px;font-weight:500;"));
    exportSpec->setObjectName(QStringLiteral("exportMessageApiSpec"));
    exportSpec->setToolTip(QStringLiteral("下载第三方系统对接用的 Markdown 接口文档，发给第三方按此规范开发对接接口"));
    sourcesHeader->addWidget(exportSpec);
    connect(exportSpec, &QPushButton::clicked, this, &MessageSettingsDialog::exportRequested);
    auto *add = button(QStringLiteral("＋ 添加接口源")); add->setFixedHeight(44);
    add->setStyleSheet(QStringLiteral("background:rgba(255,255,255,46);border:1.5px dashed rgba(255,255,255,179);border-radius:12px;color:white;font-size:14px;font-weight:700;padding:11px;"));
    add->setObjectName(QStringLiteral("addSource"));
    body->addLayout(sourcesHeader); body->addSpacing(10);
    sourcesLayout_ = new QVBoxLayout; sourcesLayout_->setSpacing(12);
    body->addLayout(sourcesLayout_);
    emptySources_=label(QStringLiteral("暂无接口源，点击下方按钮添加一个。")); emptySources_->setObjectName(QStringLiteral("emptySources"));
    emptySources_->setAlignment(Qt::AlignCenter); emptySources_->setStyleSheet(QStringLiteral("font-size:13px;color:rgba(255,255,255,235);padding:14px 0 4px;"));
    body->addWidget(emptySources_);
    for (const auto &source : config.value(QStringLiteral("messageSources")).toArray()) addSource(source.toObject());
    body->addWidget(add);
    connect(add, &QPushButton::clicked, this, [this] {
        addSource();
        QTimer::singleShot(0,this,[this]{
            if(sources_.isEmpty())return;
            if(auto *scroll=findChild<QScrollArea *>(QStringLiteral("documentScroll")))scroll->ensureWidgetVisible(sources_.last());
            sources_.last()->name->setFocus();
        });
    });
    body->addSpacing(18);
    auto *more=label(QStringLiteral("更多")); more->setFixedHeight(21); more->setStyleSheet(QStringLiteral("font-size:14px;font-weight:700;color:white;"));
    body->addWidget(more); body->addSpacing(10);
    auto *globalPanel = panel(body); globalPanel->setContentsMargins(18,14,18,14);
    auto *global = collapsible(globalPanel, QStringLiteral("全局设置"), QStringLiteral("globalSettingsToggle"), QStringLiteral("轮询间隔 / 每源条数 / 小火箭素材"));
    findChild<QPushButton *>(QStringLiteral("globalSettingsToggle"))->setStyleSheet(QStringLiteral("background:transparent;border:0;color:#3d2200;text-align:left;font-size:15px;font-weight:700;padding:0;"));
    auto *interval = new QSpinBox;
    interval->setObjectName(QStringLiteral("messagePollInterval")); interval->setRange(10,3600);
    interval->setValue(config.value(QStringLiteral("messagePollInterval")).toInt(60));
    field(global,QStringLiteral("轮询间隔"),numberControl(interval,QStringLiteral("秒")),QStringLiteral("拉取待办列表的间隔（所有源共用），10-3600 秒"));
    auto *limit = new QSpinBox;
    limit->setObjectName(QStringLiteral("messageMaxCacheItems")); limit->setRange(1,30);
    limit->setValue(config.value(QStringLiteral("messageMaxCacheItems")).toInt(30));
    field(global,QStringLiteral("每源待办条数"),numberControl(limit,QStringLiteral("条")),QStringLiteral("每个源查询并保留的最近待办条数，1-30"));
    auto *directory = directoryPicker(global,QStringLiteral("小火箭素材目录"),config.value(QStringLiteral("messageNotifyDir")).toString());
    directory->setObjectName(QStringLiteral("messageNotifyDir"));
    auto *video = new QLineEdit(config.value(QStringLiteral("messageNotifyVideo")).toString(QStringLiteral("notify-rocket.webm")));
    video->setObjectName(QStringLiteral("messageNotifyVideo")); video->setPlaceholderText(QStringLiteral("notify-rocket.webm")); video->setFixedSize(160,32);
    field(global,QStringLiteral("小火箭素材"),video,QStringLiteral("WebM 文件名，找不到时使用内置火箭"));
    connect(video,&QLineEdit::textChanged,this,[video]{setInvalid(video,false);});
    root->addSpacing(18);
    addSaveStatus(this, root, &saveStatus_, &saveStatusTimer_);
    auto *save = actionButtons(root, this);
    save->setFixedHeight(39); findChild<QPushButton *>(QStringLiteral("cancelButton"))->setFixedHeight(39);
    connect(save, &QPushButton::clicked, this, [this, enabled, interval, limit, directory, video] {
        QString firstError;
        for (int i = 0; i < sources_.size(); ++i) {
            auto *source = sources_.at(i);
            const QString name = source->name->text().trimmed();
            const QString description = name.isEmpty() ? QStringLiteral("第 %1 个源").arg(i + 1) : name;
            const bool nameInvalid = name.isEmpty();
            const bool headersInvalid = !validHeaders(source->headers->toPlainText());
            const bool urlInvalid = source->enabled->isChecked() && !validHttpUrl(source->url->text().trimmed());
            setInvalid(source->name, nameInvalid);
            setInvalid(source->headers, headersInvalid);
            setInvalid(source->url, urlInvalid);
            if (nameInvalid && firstError.isEmpty()) firstError = QStringLiteral("第 %1 个接口源名称不能为空").arg(i + 1);
            if (headersInvalid) {
                source->expandHeaders();
                if (firstError.isEmpty()) firstError = QStringLiteral("「%1」的请求头必须是 JSON 对象").arg(description);
            }
            if (urlInvalid && firstError.isEmpty()) firstError = QStringLiteral("「%1」的接口地址必须是 http 或 https URL").arg(description);
        }
        const QString videoName = video->text().trimmed().isEmpty() ? QStringLiteral("notify-rocket.webm") : video->text().trimmed();
        const bool videoInvalid = videoName.contains(QLatin1Char('/')) || videoName.contains(QLatin1Char('\\'));
        setInvalid(video, videoInvalid);
        if (videoInvalid) {
            findChild<QPushButton *>(QStringLiteral("globalSettingsToggle"))->setChecked(true);
            if (firstError.isEmpty()) firstError = QStringLiteral("小火箭素材只能填写文件名，不能包含路径分隔符");
        }
        if (!firstError.isEmpty()) { showSaveResult(false, firstError); return; }
        QJsonArray sources;
        for (const auto *source : sources_) sources.append(source->value());
        emit saved({{QStringLiteral("messageNotifyEnabled"), enabled->isChecked()}, {QStringLiteral("messageSources"), sources},
            {QStringLiteral("messagePollInterval"), interval->value()}, {QStringLiteral("messageMaxCacheItems"), limit->value()},
            {QStringLiteral("messageNotifyDir"), directory->text()}, {QStringLiteral("messageNotifyVideo"), videoName}});
    });
}

void MessageSettingsDialog::addSource(QJsonObject source) {
    auto *editor = new SourceEditor(source);
    sources_.append(editor);
    sourcesLayout_->addWidget(editor);
    sourceCount_->setText(QString::number(sources_.size()));
    emptySources_->setVisible(sources_.isEmpty());
    connect(editor->remove, &QPushButton::clicked, this, [this, editor] {
        if (activeTest_ == editor) finishConnectionTest();
        sources_.removeAll(editor);
        sourcesLayout_->removeWidget(editor);
        sourceCount_->setText(QString::number(sources_.size()));
        emptySources_->setVisible(sources_.isEmpty());
        editor->deleteLater();
    });
    connect(editor->test, &QPushButton::clicked, this, [this, editor] {
        if (activeTest_) {
            if (activeTest_ != editor) editor->testResult(QStringLiteral("请等待当前测试完成"));
            return;
        }
        setInvalid(editor->url, false);
        setInvalid(editor->headers, false);
        const QString url = editor->url->text().trimmed();
        if (!validHttpUrl(url)) {
            setInvalid(editor->url, true);
            editor->testResult(url.isEmpty() ? QStringLiteral("请先填写接口地址") : QStringLiteral("接口地址不是合法 http/https URL"));
            return;
        }
        if (!validHeaders(editor->headers->toPlainText())) {
            setInvalid(editor->headers, true);
            editor->expandHeaders();
            editor->testResult(QStringLiteral("请求头必须是 JSON 对象"));
            return;
        }
        activeTest_ = editor;
        editor->test->setEnabled(false);
        editor->test->setText(QStringLiteral("测试中..."));
        editor->result->setStyleSheet(QStringLiteral("color:#8b5e34;"));
        editor->result->setText(QStringLiteral("正在请求待办列表接口..."));
        auto value = editor->value();
        const auto *limit = findChild<QSpinBox *>(QStringLiteral("messageMaxCacheItems"));
        if (limit) value.insert(QStringLiteral("maxCacheItems"), limit->value());
        connectionTimer_->start();
        emit testRequested(value);
    });
}

void MessageSettingsDialog::connectionResult(QString id, bool ok, QString message, int count) {
    if (!activeTest_ || activeTest_->id != id) return;
    auto *editor = activeTest_;
    finishConnectionTest();
    editor->testResult(ok ? QStringLiteral("✓ 接通：列表返回 %1 条").arg(count)
                         : QStringLiteral("✗ 测试失败：") + (message.isEmpty() ? QStringLiteral("未知错误") : message), ok);
}

void MessageSettingsDialog::finishConnectionTest() {
    connectionTimer_->stop();
    if (activeTest_) {
        activeTest_->test->setEnabled(true);
        activeTest_->test->setText(QStringLiteral("测试连接"));
    }
    activeTest_ = nullptr;
}

void MessageSettingsDialog::showSaveResult(bool ok, QString error) {
    showSaveStatus(saveStatus_, saveStatusTimer_, ok, error);
}

MessageListDialog::MessageListDialog(QWidget *parent) : QDialog(parent) {
    auto *root = prepare(this, QStringLiteral("待办消息"), 560, 720);
    setMinimumSize(460, 420);
    const auto *primary = qApp->primaryScreen();
    const auto available = primary ? primary->availableGeometry() : QRect(0, 0, 1024, 768);
    resize(560, std::min(720, qRound(available.height() * .82)));
    move(available.center() - rect().center());
    root->setContentsMargins(22,20,22,16); root->setSpacing(12);
    auto *heading=findChild<QLabel *>(QStringLiteral("heading")); heading->setFixedHeight(26); heading->setStyleSheet(QStringLiteral("font-size:18px;color:white;font-weight:700;"));
    auto *header = panel(root); header->setContentsMargins(18,14,18,14); header->setSpacing(0);
    auto *top = new QHBoxLayout; top->setSpacing(12);
    count_ = label(QStringLiteral("待办消息（0）")); count_->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;"));
    top->addWidget(count_,1);
    refresh_ = button(QStringLiteral("刷新")); refresh_->setObjectName(QStringLiteral("refreshMessages"));
    refresh_->setFixedSize(74,36); refresh_->setStyleSheet(QStringLiteral("padding:7px 22px;font-family:Arial;font-size:%1pt;font-weight:700;border:1px solid rgba(224,123,32,77);").arg(14.08 * 72 / refresh_->logicalDpiY()));
    top->addWidget(refresh_); header->addLayout(top); header->addSpacing(8);
    auto *metaRow=new QHBoxLayout; metaRow->setSpacing(10);
    meta_ = label(QStringLiteral("尚未刷新"),this,QStringLiteral("hint")); meta_->setFixedHeight(18); meta_->setWordWrap(false); metaRow->addWidget(meta_,1);
    messageError_=label({},this,QStringLiteral("messageError")); messageError_->setStyleSheet(QStringLiteral("font-size:12px;color:#dc2626;"));
    messageError_->setWordWrap(false); messageError_->setAlignment(Qt::AlignRight); messageError_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred); metaRow->addWidget(messageError_,1);
    header->addLayout(metaRow); header->addSpacing(12);
    auto *sourceScroll = new QScrollArea; sourceScroll->setWidgetResizable(true);
    sourceScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); sourceScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sourceScroll->setFixedHeight(35); sourceScroll->setAccessibleName(QStringLiteral("按来源筛选"));
    auto *sourceBody = new QWidget; sourceBody->setObjectName(QStringLiteral("sourceFilters"));
    sources_ = new QHBoxLayout(sourceBody); sources_->setContentsMargins(0,0,0,4); sources_->setSpacing(8);
    sources_->setSizeConstraint(QLayout::SetMinimumSize); sourceScroll->setWidget(sourceBody); header->addWidget(sourceScroll);
    auto *main=panel(root); main->parentWidget()->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Expanding); main->setContentsMargins(18,14,18,14); main->setSpacing(10);
    root->setStretch(root->count()-1,1);
    notice_=label({},this,QStringLiteral("messageNotice")); notice_->setTextInteractionFlags(Qt::TextSelectableByMouse); main->addWidget(notice_);
    items_=new QListWidget; items_->setObjectName(QStringLiteral("messageItems")); items_->setSpacing(0);
    items_->setStyleSheet(QStringLiteral("QListWidget{background:transparent;padding:0;border:0;} QListWidget::item{background:white;border:1px solid rgba(244,164,96,87);border-radius:8px;margin:0 0 7px 0;} QListWidget::item:hover,QListWidget::item:selected{background:white;border-color:#e07b20;}"));
    items_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); items_->setSelectionMode(QAbstractItemView::SingleSelection);
    main->addWidget(items_,1);
    connect(refresh_,&QPushButton::clicked,this,&MessageListDialog::refreshRequested);
    const auto openItem = [this](QListWidgetItem *item) {
        if (!item) return;
        const auto value = item->data(Qt::UserRole).toJsonObject();
        const auto origin = value.value(QStringLiteral("origin")).toObject();
        const QUrl url(value.value(QStringLiteral("targetUrl")).toString());
        if (!url.isValid() || url.host().isEmpty() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
            notice_->setText(QStringLiteral("仅允许打开 http 或 https 链接"));
            notice_->show();
            return;
        }
        emit openRequested(origin.value(QStringLiteral("id")).toString(), value.value(QStringLiteral("id")).toString(), url);
    };
    connect(items_, &QListWidget::itemClicked, this, openItem);
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter, Qt::Key_Space}) {
        auto *shortcut = new QShortcut(QKeySequence(key), items_);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, openItem] { openItem(items_->currentItem()); });
    }
    setState({});
}

void MessageListDialog::setState(QJsonObject state) {
    state_ = std::move(state);
    bool foundFilter = filter_.isEmpty();
    for (const auto &entry : state_.value(QStringLiteral("sources")).toArray())
        if (entry.toObject().value(QStringLiteral("id")).toString() == filter_) foundFilter = true;
    if (!foundFilter) filter_.clear();
    render();
}

void MessageListDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    if (items_) QTimer::singleShot(0, this, [this] { render(); });
}

void MessageListDialog::render() {
    while (auto *item = sources_->takeAt(0)) { if (item->widget()) item->widget()->deleteLater(); delete item; }
    const auto addFilter = [this](const QString &id, const QString &title, const QString &color, int count, bool error) {
        const auto checked = id == filter_;
        const auto tint = safeColor(color);
        auto *chip = new SourceChip(title,QColor(tint),count,checked,error);
        chip->setObjectName(QStringLiteral("filter-")+(id.isEmpty()?QStringLiteral("all"):id));
        chip->setAccessibleName(QStringLiteral("筛选来源：")+title);
        sources_->addWidget(chip);
        connect(chip, &QPushButton::clicked, this, [this, id] { filter_ = id; render(); });
    };
    addFilter({}, QStringLiteral("全部"), QStringLiteral("#e07b20"), state_.value(QStringLiteral("badgeCount")).toInt(), false);
    for (const auto &entry : state_.value(QStringLiteral("sources")).toArray()) {
        const auto source = entry.toObject();
        addFilter(source.value(QStringLiteral("id")).toString(), source.value(QStringLiteral("name")).toString(QStringLiteral("未命名")),
            source.value(QStringLiteral("color")).toString(), source.value(QStringLiteral("badgeCount")).toInt()>0 ? source.value(QStringLiteral("badgeCount")).toInt() : source.value(QStringLiteral("itemCount")).toInt(), !source.value(QStringLiteral("lastError")).toString().isEmpty());
    }
    sources_->addStretch();
    count_->setText(QStringLiteral("待办消息（%1）").arg((state_.value(QStringLiteral("badgeCount")).toInt()>0 ? state_.value(QStringLiteral("badgeCount")).toInt() : state_.value(QStringLiteral("itemCount")).toInt())));
    const bool enabled = state_.value(QStringLiteral("enabled")).toBool();
    const bool configured = state_.value(QStringLiteral("configured")).toBool();
    const bool loading = state_.value(QStringLiteral("loading")).toBool();
    refresh_->setEnabled(enabled && configured && !loading);
    const auto updated = QDateTime::fromString(state_.value(QStringLiteral("lastRefreshAt")).toString(), Qt::ISODateWithMs);
    meta_->setText(loading ? QStringLiteral("正在刷新…") : updated.isValid()
        ? QStringLiteral("最后刷新 ") + updated.toLocalTime().toString(QStringLiteral("yyyy/M/d HH:mm:ss")) : QStringLiteral("尚未刷新"));
    messageError_->setText(state_.value(QStringLiteral("lastError")).toString());
    messageError_->setToolTip(messageError_->text());
    QString notice;
    if (!enabled) notice = QStringLiteral("消息提醒未启用");
    else if (!configured) notice = QStringLiteral("未配置任何启用的消息源");
    else if (state_.value(QStringLiteral("backoff")).toBool()) notice = QStringLiteral("最近同步失败，已降低轮询频率") + (state_.value(QStringLiteral("lastError")).toString().isEmpty() ? QString() : QStringLiteral("（")+state_.value(QStringLiteral("lastError")).toString()+QStringLiteral("）"));
    else if (!state_.value(QStringLiteral("lastError")).toString().isEmpty()) notice = state_.value(QStringLiteral("lastError")).toString();
    else if (filter_.isEmpty() && state_.value(QStringLiteral("hiddenCount")).toInt() > 0)
        notice = QStringLiteral("仅显示每个来源最近 %1 条").arg(state_.value(QStringLiteral("maxCacheItems")).toInt(30));
    const bool warning=enabled&&configured&&(state_.value(QStringLiteral("backoff")).toBool()||!state_.value(QStringLiteral("lastError")).toString().isEmpty());
    notice_->setStyleSheet(warning?QStringLiteral("background:#fff5f3;color:#b3261e;border:1px solid rgba(220,38,38,102);border-radius:8px;padding:8px 10px;font-size:13px;"):QStringLiteral("background:#fffaf5;color:#8b5e34;border:1px solid rgba(244,164,96,97);border-radius:8px;padding:8px 10px;font-size:13px;"));
    notice_->setText(notice); notice_->setVisible(!notice.isEmpty());
    const int scrollPosition = items_->verticalScrollBar()->value();
    items_->clear();
    for (const auto &entry : state_.value(QStringLiteral("items")).toArray()) {
        const auto value = entry.toObject();
        const auto origin = value.value(QStringLiteral("origin")).toObject();
        if (!filter_.isEmpty() && origin.value(QStringLiteral("id")).toString() != filter_) continue;
        auto *item = new QListWidgetItem(items_);
        item->setData(Qt::UserRole, value);
        auto *row = new QWidget;
        row->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *layout = new QVBoxLayout(row);
        layout->setContentsMargins(12,10,12,10);
        layout->setSpacing(5);
        auto *heading = new QHBoxLayout;
        auto *title = label(value.value(QStringLiteral("title")).toString(QStringLiteral("未命名待办")));
        title->setStyleSheet(QStringLiteral("font-size:15px;font-weight:700;"));
        title->setWordWrap(false);
        title->setMinimumWidth(0);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        title->setToolTip(title->text());
        heading->addWidget(title, 1);
        auto *tag = label(origin.value(QStringLiteral("name")).toString(QStringLiteral("未知来源")));
        tag->setMaximumWidth(145);
        tag->setWordWrap(false);
        tag->setStyleSheet(QStringLiteral("background:%1;color:white;border-radius:10px;padding:2px 9px;font-size:12px;font-weight:700;").arg(safeColor(origin.value(QStringLiteral("color")).toString())));
        heading->addWidget(tag, 0, Qt::AlignTop);
        layout->addLayout(heading);
        const auto summary = value.value(QStringLiteral("summary")).toString();
        if (!summary.isEmpty()) {
            auto *description = label(summary, row, QStringLiteral("hint"));
            description->setMaximumHeight(description->fontMetrics().lineSpacing() * 2 + 4);
            layout->addWidget(description);
        }
        auto *foot=new QHBoxLayout; foot->setSpacing(10);
        auto *level=label(value.value(QStringLiteral("level")).toString(QStringLiteral("normal")).toUpper());
        level->setStyleSheet(QStringLiteral("font-size:12px;font-weight:700;color:#6b3a00;")); foot->addWidget(level);
        auto *time=label(value.value(QStringLiteral("createdAt")).toString(),row,QStringLiteral("hint")); time->setWordWrap(false); foot->addWidget(time); foot->addStretch(); layout->addLayout(foot);
        const int rowWidth = std::max(220,items_->viewport()->width());
        item->setSizeHint(QSize(rowWidth,std::max(92,layout->heightForWidth(rowWidth)+7)));
        items_->setItemWidget(item, row);
    }
    if (items_->count() == 0) {
        auto *empty = new QListWidgetItem(filter_.isEmpty() ? QStringLiteral("暂无待办消息") : QStringLiteral("该来源暂无待办消息"), items_);
        empty->setFlags(Qt::NoItemFlags);
        empty->setTextAlignment(Qt::AlignCenter);
        empty->setSizeHint(QSize(100, 200));
    }
    items_->verticalScrollBar()->setValue(scrollPosition);
}

RulesDialog::RulesDialog(QWidget *parent) : QDialog(parent) {
    const auto *primary=qApp->primaryScreen();
    const int availableWidth=primary?primary->availableGeometry().width():1024;
    auto *root=prepare(this,QStringLiteral("计时规则说明"),std::min(900,std::max(720,availableWidth-48)),461);
    root->setAlignment(Qt::AlignTop); root->setSpacing(16);
    auto *heading=findChild<QLabel *>(QStringLiteral("heading")); heading->setAlignment(Qt::AlignCenter); heading->setFixedHeight(23);
    heading->setStyleSheet(QStringLiteral("font-size:18px;font-weight:700;color:white;"));
    auto *scroll=new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); scroll->setFixedHeight(342);
    auto *table=new QFrame; table->setObjectName(QStringLiteral("rulesTable")); table->setMinimumWidth(820);
    table->setStyleSheet(QStringLiteral("QFrame#rulesTable{background:rgba(255,255,255,235);border-radius:10px;}"));
    auto *rows=new QVBoxLayout(table); rows->setContentsMargins(0,0,0,0); rows->setSpacing(0);
    const QList<QPair<QString,QString>> rules{
        {QStringLiteral("动画正在显示"),QStringLiteral("跳过，不检测；解锁时若锁屏超过 10 分钟则计时归零")},
        {QStringLiteral("用户手动暂停监控"),QStringLiteral("跳过，不检测")},
        {QStringLiteral("屏幕已锁定"),QStringLiteral("跳过，不检测")},
        {QStringLiteral("系统休眠后唤醒"),QStringLiteral("休眠超过 10 分钟则计时归零；短时休眠继续累计")},
        {QStringLiteral("距上次鼠标/键盘操作 > 10 分钟"),QStringLiteral("计时归零（视为已休息）")},
        {QStringLiteral("距上次鼠标/键盘操作 < 3 分钟"),QStringLiteral("计时 +10 秒（视为活跃）")},
        {QStringLiteral("距上次鼠标/键盘操作 3~10 分钟"),QStringLiteral("不累加也不归零（暂停）")},
        {QStringLiteral("累计活跃时间 ≥ 设定阈值"),QStringLiteral("触发猫咪动画")}
    };
    for(int index=-1;index<rules.size();++index){
        auto *frame=new QFrame; frame->setFrameShape(QFrame::NoFrame); frame->setObjectName(QStringLiteral("ruleRow")); frame->setFixedHeight(index<0?39:index==0?37:38);
        if(index<0)frame->setStyleSheet(QStringLiteral("QFrame#ruleRow{background:rgba(244,164,96,64);border-top-left-radius:10px;border-top-right-radius:10px;}"));
        else if(index<rules.size()-1)frame->setStyleSheet(QStringLiteral("QFrame#ruleRow{background:transparent;border-bottom:1px solid rgba(244,164,96,31);}"));
        auto *cells=new QHBoxLayout(frame); cells->setContentsMargins(0,0,0,0); cells->setSpacing(0);
        auto *priority=label(index<0?QStringLiteral("优先级"):QString::number(index+1)); priority->setFixedWidth(71); priority->setAlignment(Qt::AlignCenter);
        priority->setStyleSheet(QStringLiteral("font-size:14px;font-weight:700;color:#b87333;")); cells->addWidget(priority);
        auto *condition=label(index<0?QStringLiteral("条件"):rules.at(index).first); condition->setWordWrap(false);
        condition->setStyleSheet(QStringLiteral("font-size:14px;padding:0 14px;")+(index<0?QStringLiteral("font-weight:700;color:#5a3e1b;"):QString()));
        auto *behavior=label(index<0?QStringLiteral("行为"):rules.at(index).second); behavior->setWordWrap(false);
        behavior->setStyleSheet(condition->styleSheet()); cells->addWidget(condition,326); cells->addWidget(behavior,505); rows->addWidget(frame);
    }
    scroll->setWidget(table); root->addWidget(scroll);
    auto *note=label(QStringLiteral("每 10 秒检测一次，按优先级从上到下判断。")); note->setFixedHeight(18); note->setAlignment(Qt::AlignCenter);
    note->setStyleSheet(QStringLiteral("color:rgba(255,255,255,179);font-size:12px;")); root->addWidget(note);
}

} // namespace purr
