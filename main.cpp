#include <QtWidgets>
#include <cmath>
#include <functional>
#include <array>
#include <optional>

static QString root;
#ifndef HYPER_LINUX_VERSION
#define HYPER_LINUX_VERSION "0.2.0"
#endif

static QPair<QString, QStringList> backendCommand(QString argument) {
    const QString bundled = root + "/hyper-linux-backend";
    if (QFileInfo(bundled).isExecutable()) return {bundled, {argument}};
    return {"/usr/bin/python3", {root + "/backend.py", argument}};
}
static const std::array<int, 10> frequencies{32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

// Original illustration. No vendor logos, product photos or bundled fonts.
static QPixmap headsetIllustration() {
    QPixmap art(660, 620); art.fill(Qt::transparent);
    QPainter p(&art); p.setRenderHint(QPainter::Antialiasing); p.scale(2, 2);
    QPainterPath band; band.moveTo(67, 190); band.cubicTo(43, 13, 284, 13, 260, 190);
    p.setPen(QPen(QColor("#0d0e12"), 34, Qt::SolidLine, Qt::RoundCap)); p.drawPath(band);
    p.setPen(QPen(QColor("#666873"), 3, Qt::SolidLine, Qt::RoundCap)); p.drawPath(band);
    QPainterPath padding; padding.moveTo(78, 173); padding.cubicTo(62, 42, 266, 42, 249, 173);
    p.setPen(QPen(QColor("#373943"), 13, Qt::SolidLine, Qt::RoundCap)); p.drawPath(padding);
    p.setPen(QPen(QColor("#e82e48"), 5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(60, 157), QPointF(66, 212)); p.drawLine(QPointF(268, 157), QPointF(262, 212));
    for (int x : {49, 221}) {
        p.save(); p.translate(x + 30, 216); p.rotate(x == 49 ? -13 : 13);
        p.setPen(QPen(QColor("#555764"), 2));
        QLinearGradient shell(-34, -52, 34, 52); shell.setColorAt(0, QColor("#42454f")); shell.setColorAt(1, QColor("#15161c"));
        p.setBrush(shell); p.drawRoundedRect(QRectF(-34, -55, 68, 112), 28, 28);
        p.setPen(QPen(QColor("#121319"), 8)); p.setBrush(QColor("#24262e"));
        p.drawRoundedRect(QRectF(-22, -44, 44, 90), 19, 19);
        p.restore();
    }
    QPainterPath mic; mic.moveTo(61, 230); mic.cubicTo(43, 272, 108, 286, 133, 270);
    p.setPen(QPen(QColor("#898b95"), 6, Qt::SolidLine, Qt::RoundCap)); p.drawPath(mic);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#383b45")); p.drawRoundedRect(QRectF(124, 260, 33, 17), 8, 8);
    return art;
}

class Curve : public QWidget {
public:
    std::array<double, 10> gains{};
    Curve() { setMinimumHeight(170); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF area(34, 15, width() - 50, height() - 38);
        p.setPen(QColor("#303036"));
        for (int y = 0; y <= 4; ++y) {
            double yy = area.top() + area.height() * y / 4;
            p.drawLine(QPointF(area.left(), yy), QPointF(area.right(), yy));
            p.setPen(QColor("#85858e"));
            p.drawText(QRectF(0, yy - 8, 28, 18), Qt::AlignRight, QString::number(12 - y * 6));
            p.setPen(QColor("#303036"));
        }
        QPainterPath line;
        for (int i = 0; i < 10; ++i) {
            double x = area.left() + area.width() * i / 9;
            double y = area.center().y() - gains[i] * area.height() / 24;
            if (i == 0) line.moveTo(x, y); else line.lineTo(x, y);
        }
        auto fill = line;
        fill.lineTo(area.bottomRight()); fill.lineTo(area.bottomLeft()); fill.closeSubpath();
        QLinearGradient gradient(area.topLeft(), area.bottomLeft());
        gradient.setColorAt(0, QColor(229, 30, 51, 65)); gradient.setColorAt(1, QColor(229, 30, 51, 0));
        p.fillPath(fill, gradient);
        p.setPen(QPen(QColor("#f2364c"), 2)); p.drawPath(line);
        p.setBrush(QColor("#f2364c"));
        for (int i = 0; i < 10; ++i)
            p.drawEllipse(QPointF(area.left() + area.width() * i / 9,
                          area.center().y() - gains[i] * area.height() / 24), 3, 3);
    }
};

class Window : public QMainWindow {
    QProcess backend;
    QByteArray buffer;
    int serial = 0;
    QMap<int, std::function<void(QJsonObject)>> pending;
    QStackedWidget *pages;
    QLabel *status, *battery, *connection, *notice, *usbDetail, *audioNotice, *eqNotice, *loopNotice, *image;
    QCheckBox *mute, *sidetone, *loopback;
    QSpinBox *sleep, *loopVolume;
    QComboBox *sinks, *sources, *presets;
    QPlainTextEdit *diagnostics;
    QSlider *outputVolume, *inputVolume;
    std::array<QSlider *, 10> bandSliders;
    std::array<QLineEdit *, 10> bandFreqs, bandQs;
    Curve *curve;
    QPushButton *eqApply, *eqStop;
    bool loading = false, preview = false, eqActive = false;
    std::optional<bool> lastHardwareMute;
    QJsonObject currentState;
    QTimer eqDebounce;

    QLabel *text(QString value, QString name = "") {
        auto *label = new QLabel(value);
        label->setObjectName(name); label->setWordWrap(true);
        return label;
    }
    QFrame *card(QString title, QString description, QVBoxLayout **content) {
        auto *frame = new QFrame; frame->setObjectName("card");
        auto *layout = new QVBoxLayout(frame); layout->setContentsMargins(22, 20, 22, 20); layout->setSpacing(12);
        layout->addWidget(text(title, "cardTitle"));
        if (!description.isEmpty()) layout->addWidget(text(description, "muted"));
        *content = layout;
        return frame;
    }
    void send(QJsonObject request, std::function<void(QJsonObject)> callback = {}) {
        if (preview || backend.state() != QProcess::Running) {
            if (!preview) showError("The local service is not running.");
            return;
        }
        int id = ++serial; request["id"] = id;
        if (callback) pending.insert(id, callback);
        backend.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
    }
    void showError(QString error) { notice->setText(error); notice->setProperty("error", true); notice->show(); }
    QString target(QComboBox *box) { return box->currentData().toString(); }
    void fillDevices(QComboBox *box, QJsonArray devices) {
        QString previous = target(box); box->clear();
        for (auto value : devices) {
            auto device = value.toObject(); box->addItem(device["description"].toString(), device["name"].toString());
        }
        int index = box->findData(previous); if (index >= 0) box->setCurrentIndex(index);
        if (!box->count()) box->addItem("Device not found", "");
    }
    void updateAudio(QJsonObject data, bool refill = false) {
        loading = true;
        if (refill) { fillDevices(sinks, data["sinks"].toArray()); fillDevices(sources, data["sources"].toArray()); }
        for (auto item : data["sinks"].toArray()) {
            auto d = item.toObject(); if (d["name"].toString() == target(sinks)) outputVolume->setValue(d["volume"].toInt());
        }
        for (auto item : data["sources"].toArray()) {
            auto d = item.toObject(); if (d["name"].toString() == target(sources)) {
                inputVolume->setValue(d["volume"].toInt()); mute->setChecked(d["mute"].toBool());
            }
        }
        loading = false;
        bool available = !target(sinks).isEmpty() && !target(sources).isEmpty();
        loopback->setEnabled(available); mute->setEnabled(!target(sources).isEmpty() || currentState["connected"].toBool());
        outputVolume->setEnabled(!target(sinks).isEmpty()); inputVolume->setEnabled(!target(sources).isEmpty());
        eqApply->setEnabled(!target(sinks).isEmpty());
        audioNotice->setText(available ? "Audio connected to PipeWire" : "Refresh devices with the dongle connected.");
    }
    void refreshAudio() {
        send({{"action", "audio_devices"}}, [this](QJsonObject result) {
            if (result["ok"].toBool()) {
                updateAudio(result["data"].toObject(), true);
            }
            else audioNotice->setText(result["error"].toString());
        });
    }
    void audioSet(QString kind, QJsonObject values) {
        if (loading) return;
        values["action"] = "audio_set"; values["kind"] = kind;
        values["target"] = target(kind == "sink" ? sinks : sources);
        send(values, [this](QJsonObject result) {
            if (result["ok"].toBool()) updateAudio(result["data"].toObject());
            else { showError(result["error"].toString()); refreshAudio(); }
        });
    }
    void updateState(QJsonObject state) {
        currentState = state;
        loading = true;
        bool ready = state["usb_ready"].toBool() && state["connected"].isBool() && state["connected"].toBool();
        bool hasConnection = state["connected"].isBool();
        connection->setText(ready ? "●  CONNECTED" : hasConnection ? "○  HEADSET OFF" : "○  WAITING FOR HEADSET");
        status->setText(ready ? "Your Cloud III, on Linux." : "Connect the dongle and turn on your headset.");
        battery->setText(state["battery"].isDouble() ? QString::number(state["battery"].toInt()) + "%" : "—");
        if (state["charging"].isDouble() && state["charging"].toInt() == 1) battery->setText(battery->text() + " · charging");
        if (state["charging"].isDouble() && state["charging"].toInt() == 2) battery->setText(battery->text() + " · fully charged");
        mute->setEnabled(ready || !target(sources).isEmpty()); sidetone->setEnabled(ready); sleep->setEnabled(ready);
        if (target(sources).isEmpty()) mute->setChecked(state["mute"].toBool());
        sidetone->setChecked(state["sidetone"].toBool());
        sidetone->setText(state["sidetone"].isBool() ? "Hear my voice in the headset" : "Hear my voice in the headset · state unknown");
        if (state["sleep"].isDouble()) sleep->setValue(state["sleep"].toInt());
        auto dev = state["device"].toObject();
        usbDetail->setText(state["usb_ready"].toBool() ? QString("%1 · %2 bytes · %3").arg(
            state["transport"].toString()).arg(state["packet_size"].toInt()).arg(dev["path"].toString()) : "USB communication has not been confirmed");
        bool changedMute = state["mute"].isBool() && (!lastHardwareMute.has_value() || *lastHardwareMute != state["mute"].toBool());
        if (state["mute"].isBool()) lastHardwareMute = state["mute"].toBool(); else lastHardwareMute.reset();
        loading = false;
        if (changedMute && !target(sources).isEmpty())
            send({{"action", "audio_devices"}}, [this](QJsonObject r) { if (r["ok"].toBool()) updateAudio(r["data"].toObject()); });
    }
    void setHardware(QString name, QJsonValue value) {
        if (loading) return;
        notice->setText("Sending command and waiting for headset confirmation…"); notice->show();
        send({{"action", "set"}, {"name", name}, {"value", value}}, [this](QJsonObject r) {
            if (r["ok"].toBool()) notice->setText(QString("Setting confirmed by headset · %1 ms").arg(r["data"].toObject()["elapsed_ms"].toInt()));
            else showError(r["error"].toString());
        });
    }
    QJsonArray bands() {
        QJsonArray result;
        for (int i = 0; i < 10; ++i) result.append(QJsonObject{{"frequency", bandFreqs[i]->text().toDouble()},
            {"gain", bandSliders[i]->value() / 10.0}, {"q", bandQs[i]->text().toDouble()}});
        return result;
    }
    void applyEq() {
        for (int i = 0; i < 10; ++i) {
            if (!bandFreqs[i]->hasAcceptableInput() || !bandQs[i]->hasAcceptableInput()) {
                eqNotice->setText("Check each band: frequency 20–20000 Hz and Q 0.1–12. Use a decimal point.");
                return;
            }
        }
        eqNotice->setText("Starting equalizer…");
        send({{"action", "eq"}, {"enabled", true}, {"sink", target(sinks)}, {"bands", bands()}}, [this](QJsonObject r) {
            eqActive = r["ok"].toBool(); eqStop->setEnabled(eqActive);
            eqNotice->setText(eqActive ? "Active. Select “Hyper Linux · Equalizer” as the output in your sound settings. Closing the app stops the effect."
                                        : r["error"].toString());
        });
    }
    void loadPreset(QJsonObject profile) {
        auto values = profile["bands"].toArray();
        if (values.size() != 10) { showError("Invalid profile: 10 bands are required."); return; }
        for (auto value : values) {
            auto b = value.toObject();
            if (!b["frequency"].isDouble() || !b["gain"].isDouble() || !b["q"].isDouble()
                || !std::isfinite(b["frequency"].toDouble()) || b["frequency"].toDouble() < 20 || b["frequency"].toDouble() > 20000
                || !std::isfinite(b["gain"].toDouble()) || std::abs(b["gain"].toDouble()) > 12
                || !std::isfinite(b["q"].toDouble()) || b["q"].toDouble() < .1 || b["q"].toDouble() > 12) {
                showError("Invalid profile: frequency, gain or Q is out of range."); return;
            }
        }
        loading = true;
        for (int i = 0; i < 10; ++i) {
            auto b = values[i].toObject(); bandSliders[i]->setValue(qRound(b["gain"].toDouble() * 10));
            bandFreqs[i]->setText(QString::number(b["frequency"].toDouble(), 'f', 0)); bandQs[i]->setText(QString::number(b["q"].toDouble(), 'f', 1));
            curve->gains[i] = b["gain"].toDouble();
        }
        curve->update(); loading = false;
        if (eqActive) eqDebounce.start();
    }
    QWidget *devicePage() {
        auto *page = new QWidget; auto *outer = new QVBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(18);
        auto *row = new QHBoxLayout; row->setSpacing(18);
        QVBoxLayout *visual; auto *product = card("READY TO PLAY.", "Cloud III Wireless · 2.4 GHz", &visual);
        product->setMinimumWidth(310);
        image = new QLabel; image->setAlignment(Qt::AlignCenter);
        QPixmap pix = headsetIllustration();
        image->setPixmap(pix.scaled(330, 310, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        visual->addWidget(image, 1);
        visual->addWidget(text("BATTERY", "eyebrow")); battery = text("—", "battery"); visual->addWidget(battery);
        visual->addWidget(text("Reported by your headset. No estimates.", "muted"));
        row->addWidget(product, 5);
        auto *controls = new QVBoxLayout; controls->setSpacing(18);
        QVBoxLayout *mic; auto *micCard = card("Microphone", "One mute control, integrated with your system.", &mic);
        mute = new QCheckBox("Mute microphone"); mute->setEnabled(false); mic->addWidget(mute);
        mic->addWidget(text("Follows the physical headset button and Linux sound settings.", "muted"));
        connect(mute, &QCheckBox::clicked, this, [this](bool b) {
            if (!target(sources).isEmpty()) audioSet("source", {{"mute", b}});
            else setHardware("mute", b);
        });
        controls->addWidget(micCard);
        QVBoxLayout *side; auto *sideCard = card("Microphone monitoring", "Hardware sidetone, without routing audio through your computer.", &side);
        sidetone = new QCheckBox("Hear my voice in the headset"); sidetone->setEnabled(false); side->addWidget(sidetone);
        side->addWidget(text("If sidetone is not audible, try software monitoring on the Audio tab.", "muted"));
        connect(sidetone, &QCheckBox::clicked, this, [this](bool b) { setHardware("sidetone", b); });
        controls->addWidget(sideCard);
        QVBoxLayout *power; auto *powerCard = card("Auto power-off", "Idle timeout. Set 0 to keep the headset on.", &power);
        auto *powerRow = new QHBoxLayout; sleep = new QSpinBox; sleep->setRange(0, 255); sleep->setSuffix(" min"); sleep->setSpecialValueText("Never"); sleep->setEnabled(false);
        auto *save = new QPushButton("Apply"); powerRow->addWidget(sleep); powerRow->addWidget(save); power->addLayout(powerRow);
        connect(save, &QPushButton::clicked, this, [this] { if (sleep->isEnabled()) setHardware("sleep", sleep->value()); });
        controls->addWidget(powerCard); row->addLayout(controls, 6); outer->addLayout(row);
        usbDetail = text("USB communication has not been confirmed", "muted"); outer->addWidget(usbDetail); outer->addStretch();
        return page;
    }
    QWidget *audioPage() {
        auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(18);
        QVBoxLayout *devices; auto *deviceCard = card("Audio devices", "Select your headset’s physical input and output.", &devices);
        auto *grid = new QGridLayout; sinks = new QComboBox; sources = new QComboBox;
        sinks->setMinimumContentsLength(15); sources->setMinimumContentsLength(15);
        sinks->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); sources->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        grid->addWidget(text("Output"), 0, 0); grid->addWidget(sinks, 0, 1); grid->addWidget(text("Microphone"), 1, 0); grid->addWidget(sources, 1, 1);
        outputVolume = new QSlider(Qt::Horizontal); outputVolume->setRange(0, 100); outputVolume->setEnabled(false);
        inputVolume = new QSlider(Qt::Horizontal); inputVolume->setRange(0, 100); inputVolume->setEnabled(false);
        grid->addWidget(text("Headset volume"), 2, 0); grid->addWidget(outputVolume, 2, 1);
        grid->addWidget(text("Microphone gain"), 3, 0); grid->addWidget(inputVolume, 3, 1);
        devices->addLayout(grid); audioNotice = text("Refresh devices with the dongle connected.", "muted"); devices->addWidget(audioNotice);
        auto *refresh = new QPushButton("Refresh devices"); devices->addWidget(refresh, 0, Qt::AlignLeft);
        connect(refresh, &QPushButton::clicked, this, [this] { refreshAudio(); });
        connect(outputVolume, &QSlider::sliderReleased, this, [this] { audioSet("sink", {{"volume", outputVolume->value()}}); });
        connect(inputVolume, &QSlider::sliderReleased, this, [this] { audioSet("source", {{"volume", inputVolume->value()}}); });
        layout->addWidget(deviceCard);
        QVBoxLayout *monitor; auto *monitorCard = card("Hear my microphone", "Local monitoring through PipeWire. You may hear a small audio delay.", &monitor);
        loopback = new QCheckBox("Enable software monitoring"); loopback->setEnabled(false); monitor->addWidget(loopback);
        auto *volumeRow = new QHBoxLayout; volumeRow->addWidget(text("Monitor volume")); loopVolume = new QSpinBox; loopVolume->setRange(0, 100); loopVolume->setValue(25); loopVolume->setSuffix("%"); volumeRow->addWidget(loopVolume); volumeRow->addStretch(); monitor->addLayout(volumeRow);
        loopNotice = text("Start at a low volume. Use one monitoring mode at a time to avoid echo.", "muted"); monitor->addWidget(loopNotice);
        auto applyLoop = [this] {
            if (loading) return;
            bool enabled = loopback->isChecked(); loopNotice->setText("Applying monitoring settings…");
            send({{"action", "loopback"}, {"enabled", enabled}, {"source", target(sources)}, {"sink", target(sinks)}, {"volume", loopVolume->value()}},
                [this, enabled](QJsonObject r) {
                    loading = true; loopback->setChecked(r["ok"].toBool() && enabled); loading = false;
                    loopNotice->setText(r["ok"].toBool() ? enabled ? "Local monitoring is active. Closing the app stops it." : "Monitoring is off."
                                                                  : r["error"].toString());
                });
        };
        connect(loopback, &QCheckBox::clicked, this, applyLoop);
        connect(loopVolume, &QSpinBox::editingFinished, this, [this, applyLoop] { if (loopback->isChecked()) applyLoop(); });
        layout->addWidget(monitorCard);
        QVBoxLayout *compat; auto *compatCard = card("NGENUITY features", "The proprietary Windows audio engine has not been ported.", &compat);
        compat->addWidget(text("Native EQ is on the next tab. DTS/SONAMI, AI voice processing and firmware updates are not available in this version.", "muted"));
        layout->addWidget(compatCard); layout->addStretch();
        return page;
    }
    QWidget *eqPage() {
        auto *page = new QWidget; auto *outer = new QVBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(18);
        QVBoxLayout *content; auto *frame = card("Equalizer", "10 bands with adjustable frequency and Q. Audio is processed on Linux.", &content);
        auto *top = new QHBoxLayout; presets = new QComboBox; presets->addItems({"Flat", "Bass", "Voice", "Custom"});
        top->addWidget(text("Profile")); top->addWidget(presets); top->addStretch();
        auto *save = new QPushButton("Save profile"); auto *load = new QPushButton("Open profile"); top->addWidget(load); top->addWidget(save); content->addLayout(top);
        curve = new Curve; content->addWidget(curve);
        auto *grid = new QGridLayout; grid->setHorizontalSpacing(9);
        grid->addWidget(text("Hz", "muted"), 2, 0); grid->addWidget(text("Q", "muted"), 3, 0);
        for (int i = 0; i < 10; ++i) {
            auto *gain = new QLabel("0.0 dB"); gain->setAlignment(Qt::AlignCenter); gain->setObjectName("muted"); grid->addWidget(gain, 0, i + 1);
            auto *slider = new QSlider(Qt::Vertical); slider->setRange(-120, 120); slider->setMinimumHeight(110); bandSliders[i] = slider; grid->addWidget(slider, 1, i + 1, Qt::AlignHCenter);
            auto *freq = new QLineEdit(QString::number(frequencies[i]));
            auto *freqValidator = new QDoubleValidator(20, 20000, 0, freq); freqValidator->setLocale(QLocale::c()); freqValidator->setNotation(QDoubleValidator::StandardNotation); freq->setValidator(freqValidator);
            freq->setFixedWidth(64); freq->setAlignment(Qt::AlignCenter); freq->setToolTip("Frequency in Hz (20–20000)"); bandFreqs[i] = freq; grid->addWidget(freq, 2, i + 1, Qt::AlignHCenter);
            auto *q = new QLineEdit("1.4");
            auto *qValidator = new QDoubleValidator(.1, 12, 1, q); qValidator->setLocale(QLocale::c()); qValidator->setNotation(QDoubleValidator::StandardNotation); q->setValidator(qValidator);
            q->setFixedWidth(64); q->setAlignment(Qt::AlignCenter); q->setToolTip("Q (0.1–12)"); bandQs[i] = q; grid->addWidget(q, 3, i + 1, Qt::AlignHCenter);
            connect(slider, &QSlider::valueChanged, this, [this, i, gain](int value) {
                gain->setText(QString::number(value / 10.0, 'f', 1) + " dB"); curve->gains[i] = value / 10.0; curve->update();
                if (!loading) { presets->setCurrentIndex(3); if (eqActive) eqDebounce.start(); }
            });
            connect(freq, &QLineEdit::editingFinished, this, [this] { if (eqActive && !loading) eqDebounce.start(); });
            connect(q, &QLineEdit::editingFinished, this, [this] { if (eqActive && !loading) eqDebounce.start(); });
        }
        content->addLayout(grid);
        content->addWidget(text("The curve shows gain per band. Filters use the frequency and Q values above. Automatic headroom reduces volume to prevent clipping.", "muted"));
        auto *actions = new QHBoxLayout; eqApply = new QPushButton("Enable / apply"); eqApply->setObjectName("primary"); eqApply->setEnabled(false);
        eqStop = new QPushButton("Disable"); eqStop->setEnabled(false); actions->addWidget(eqApply); actions->addWidget(eqStop); actions->addStretch(); content->addLayout(actions);
        eqNotice = text("Select the physical output on the Audio tab. Once enabled, select the equalizer output in your sound settings.", "muted"); content->addWidget(eqNotice);
        outer->addWidget(frame); outer->addStretch();
        connect(eqApply, &QPushButton::clicked, this, [this] { applyEq(); });
        connect(eqStop, &QPushButton::clicked, this, [this] {
            eqDebounce.stop(); send({{"action", "eq"}, {"enabled", false}}, [this](QJsonObject r) {
                if (r["ok"].toBool()) { eqActive = false; eqStop->setEnabled(false); eqNotice->setText("Equalizer is off."); }
            });
        });
        connect(presets, &QComboBox::activated, this, [this](int preset) {
            if (preset == 3) return;
            const std::array<double, 10> bass{5, 4, 2, 0, 0, 0, 0, 0, 0, 0};
            const std::array<double, 10> voice{-4, -3, -2, 0, 1, 2, 3, 2, 0, -1};
            QJsonArray values;
            for (int i = 0; i < 10; ++i) values.append(QJsonObject{{"frequency", frequencies[i]}, {"gain", preset == 1 ? bass[i] : preset == 2 ? voice[i] : 0}, {"q", 1.4}});
            loadPreset({{"bands", values}});
        });
        connect(save, &QPushButton::clicked, this, [this] {
            auto name = QFileDialog::getSaveFileName(this, "Save profile", QDir::homePath() + "/cloud-iii.json", "Profile (*.json)");
            if (name.isEmpty()) return;
            QSaveFile file(name); if (!file.open(QIODevice::WriteOnly)) { showError(file.errorString()); return; }
            file.write(QJsonDocument(QJsonObject{{"format", "hyper-linux-eq-v1"}, {"bands", bands()}}).toJson());
            if (!file.commit()) showError(file.errorString());
        });
        connect(load, &QPushButton::clicked, this, [this] {
            auto name = QFileDialog::getOpenFileName(this, "Open profile", QDir::homePath(), "Profile (*.json)"); if (name.isEmpty()) return;
            QFile file(name); if (!file.open(QIODevice::ReadOnly) || file.size() > 100000) { showError("Could not read this profile."); return; }
            QJsonParseError error; auto document = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject() || document.object()["format"].toString() != "hyper-linux-eq-v1") { showError("Invalid profile format."); return; }
            loadPreset(document.object()); presets->setCurrentIndex(3);
        });
        eqDebounce.setSingleShot(true); eqDebounce.setInterval(700);
        connect(&eqDebounce, &QTimer::timeout, this, [this] { if (eqActive) applyEq(); });
        return page;
    }
    QWidget *diagnosticPage() {
        auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(text("Diagnostics", "cardTitle"));
        layout->addWidget(text("Logs contain USB commands and device state only. Microphone audio is never recorded.", "muted"));
        diagnostics = new QPlainTextEdit; diagnostics->setReadOnly(true); layout->addWidget(diagnostics, 1);
        auto *buttons = new QHBoxLayout; auto *inspect = new QPushButton("Generate diagnostics"); auto *logs = new QPushButton("Open log folder");
        buttons->addWidget(inspect); buttons->addWidget(logs); buttons->addStretch(); layout->addLayout(buttons);
        connect(inspect, &QPushButton::clicked, this, [this] {
            send({{"action", "diagnose"}}, [this](QJsonObject r) { diagnostics->setPlainText(QString::fromUtf8(QJsonDocument(r).toJson())); });
        });
        connect(logs, &QPushButton::clicked, this, [] {
            QString folder = qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + "/.local/state") + "/hyper-linux";
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        });
        layout->addWidget(text("Independent experimental release. Protocol reconstructed from NGENUITY 3.1.0 and tested with a Cloud III Wireless dongle.", "muted"));
        return page;
    }
    void message(QJsonObject obj) {
        QString type = obj["type"].toString();
        if (type == "state") updateState(obj);
        else if (type == "audio_state") updateAudio(obj["data"].toObject());
        else if (type == "error") showError(obj["message"].toString());
        else if (type == "result") {
            int id = obj["id"].toInt();
            if (pending.contains(id)) pending.take(id)(obj);
            else if (!obj["ok"].toBool()) showError(obj["error"].toString());
        } else if (type == "process_stopped") {
            if (obj["name"] == "loop") { loopback->setChecked(false); loopNotice->setText("PipeWire stopped monitoring. Check the logs."); }
            if (obj["name"] == "eq") { eqActive = false; eqStop->setEnabled(false); eqNotice->setText("PipeWire stopped the equalizer. Check the logs."); }
        }
    }
public:
    Window(bool isPreview) : preview(isPreview) {
        setWindowTitle("Hyper Linux · Cloud III Wireless"); resize(1180, 840); setMinimumSize(980, 720);
        auto *central = new QWidget; setCentralWidget(central); auto *shell = new QHBoxLayout(central); shell->setContentsMargins(0, 0, 0, 0); shell->setSpacing(0);
        auto *sidebar = new QFrame; sidebar->setObjectName("sidebar"); sidebar->setFixedWidth(190);
        auto *nav = new QVBoxLayout(sidebar); nav->setContentsMargins(20, 30, 20, 24); nav->setSpacing(9);
        nav->addWidget(text("hyper", "brand")); nav->addWidget(text("LINUX", "brandSub")); nav->addSpacing(36);
        auto *group = new QButtonGroup(this); group->setExclusive(true);
        QStringList names{"Device", "Audio", "Equalizer", "Diagnostics"};
        pages = new QStackedWidget;
        for (int i = 0; i < names.size(); ++i) {
            auto *button = new QPushButton(names[i]); button->setCheckable(true); button->setObjectName("nav"); group->addButton(button, i); nav->addWidget(button);
            if (i == 0) button->setChecked(true);
        }
        nav->addStretch(); nav->addWidget(text("CLOUD III\nWIRELESS", "eyebrow")); nav->addSpacing(8); nav->addWidget(text(QString("Community edition\nExperimental · %1").arg(HYPER_LINUX_VERSION), "muted")); shell->addWidget(sidebar);
        auto *main = new QVBoxLayout; main->setContentsMargins(28, 26, 28, 22); main->setSpacing(14);
        auto *header = new QHBoxLayout; auto *heading = new QVBoxLayout;
        heading->addWidget(text("CLOUD III WIRELESS", "heading")); status = text("Connect the dongle and turn on your headset.", "muted"); heading->addWidget(status); header->addLayout(heading, 1);
        auto *right = new QVBoxLayout; connection = text("○  WAITING FOR HEADSET", "eyebrow"); connection->setMinimumWidth(175); right->addWidget(connection);
        auto *refresh = new QPushButton("Reconnect"); right->addWidget(refresh); header->addLayout(right); main->addLayout(header);
        notice = text("", "notice"); notice->setTextInteractionFlags(Qt::TextSelectableByMouse); notice->hide(); main->addWidget(notice);
        pages->addWidget(devicePage()); pages->addWidget(audioPage()); pages->addWidget(eqPage()); pages->addWidget(diagnosticPage());
        auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(pages); main->addWidget(scroll, 1);
        main->addWidget(text("HYPER LINUX  /  LOCAL CONTROL  /  CLOUD III WIRELESS", "footer")); shell->addLayout(main, 1);
        connect(group, &QButtonGroup::idClicked, pages, &QStackedWidget::setCurrentIndex);
        connect(pages, &QStackedWidget::currentChanged, group, [group](int index) {
            if (auto *button = group->button(index)) button->setChecked(true);
        });
        connect(refresh, &QPushButton::clicked, this, [this] { send({{"action", "refresh"}}); refreshAudio(); });
        setStyleSheet(R"(
            QMainWindow,QWidget { background:#151517; color:#efeff2; font-size:14px; }
            QFrame#sidebar { background:#0c0c0e; border-right:1px solid #28282d; }
            QFrame#sidebar QLabel { background:transparent; }
            QFrame#card { background:#202023; border:1px solid #303035; border-radius:12px; }
            QFrame#card QLabel,QFrame#card QCheckBox { background:transparent; }
            QLabel#heading { font-size:30px; font-weight:700; letter-spacing:1px; }
            QLabel#brand { color:#efeff2; font-size:34px; font-weight:700; }
            QLabel#brandSub { color:#efeff2; font-size:15px; letter-spacing:8px; }
            QLabel#cardTitle { font-size:20px; font-weight:700; }
            QLabel#battery { font-size:40px; font-weight:700; }
            QLabel#muted { color:#a3a3ad; font-size:13px; }
            QLabel#eyebrow { color:#b9b9c2; font-size:11px; font-weight:700; letter-spacing:1px; }
            QLabel#footer { color:#65656e; font-size:10px; letter-spacing:1px; }
            QLabel#notice { background:#2c2227; color:#f4b8c0; border:1px solid #63303c; border-radius:6px; padding:10px; }
            QPushButton { background:#2c2c31; border:1px solid #3c3c44; border-radius:7px; padding:9px 15px; }
            QPushButton:hover { background:#3a3a42; }
            QPushButton:disabled { color:#62626c; background:#242428; border-color:#2b2b30; }
            QPushButton#primary { background:#d92740; border-color:#ed3750; color:white; }
            QPushButton#nav { text-align:left; border:0; background:transparent; padding:13px 14px; color:#9898a2; }
            QPushButton#nav:checked { background:#302024; color:#ff5369; border-left:3px solid #f2364c; }
            QPushButton#nav:hover { background:#222226; }
            QCheckBox { spacing:10px; padding:4px 0; }
            QCheckBox:disabled { color:#777781; }
            QCheckBox::indicator { width:19px; height:19px; border:1px solid #62626c; border-radius:5px; background:#151517; }
            QCheckBox::indicator:checked { background:#e92e47; border:4px solid #e92e47; image:none; }
            QCheckBox::indicator:disabled { border-color:#3b3b42; background:#232328; }
            QSpinBox,QDoubleSpinBox,QComboBox,QPlainTextEdit,QLineEdit { background:#151517; color:#efeff2; border:1px solid #3b3b43; border-radius:5px; padding:6px; selection-background-color:#a62438; }
            QLineEdit { font-size:12px; }
            QPushButton#primary:disabled { background:#46232b; border-color:#63303c; color:#9e717a; }
            QSlider::groove:horizontal { height:4px; background:#43434d; border-radius:2px; }
            QSlider::handle:horizontal { width:14px; margin:-5px 0; border-radius:7px; background:#ef3750; }
            QSlider::groove:vertical { width:3px; background:#43434d; }
            QSlider::handle:vertical { height:12px; margin:0 -5px; border-radius:5px; background:#ef3750; }
            QScrollArea { border:0; } QScrollBar:vertical { background:#171719; width:8px; } QScrollBar::handle:vertical { background:#414149; border-radius:4px; min-height:30px; }
            QToolTip { background:#29292e; color:white; border:1px solid #51515c; }
        )");
        if (preview) { status->setText("Interface preview · no hardware connection"); return; }
        connect(&backend, &QProcess::readyReadStandardOutput, this, [this] {
            buffer += backend.readAllStandardOutput();
            while (buffer.contains('\n')) {
                int n = buffer.indexOf('\n'); auto line = buffer.left(n); buffer.remove(0, n + 1);
                QJsonParseError error; auto doc = QJsonDocument::fromJson(line, &error);
                if (error.error == QJsonParseError::NoError && doc.isObject()) message(doc.object());
            }
        });
        connect(&backend, &QProcess::readyReadStandardError, this, [this] { diagnostics->appendPlainText(QString::fromUtf8(backend.readAllStandardError())); });
        connect(&backend, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { showError("Could not start the local service: " + backend.errorString()); });
        connect(&backend, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            updateState({}); pending.clear(); loopback->setChecked(false); eqActive = false;
            showError("The local service stopped. Close and reopen the app.");
        });
        connect(&backend, &QProcess::started, this, [this] { send({{"action", "refresh"}}); refreshAudio(); });
        auto command = backendCommand("--server");
        backend.start(command.first, command.second);
    }
    void setPage(int page) { pages->setCurrentIndex(page); }
    ~Window() override {
        backend.disconnect(this);
        if (backend.state() == QProcess::Running) {
            backend.write("{\"action\":\"quit\"}\n");
            if (!backend.waitForFinished(2500)) { backend.terminate(); if (!backend.waitForFinished(500)) backend.kill(); }
        }
    }
};

int main(int argc, char **argv) {
    QStringList rawArgs;
    for (int i = 1; i < argc; ++i) rawArgs.append(QString::fromLocal8Bit(argv[i]));
    if (rawArgs.contains("--version")) { QTextStream(stdout) << "Hyper Linux " << HYPER_LINUX_VERSION << '\n'; return 0; }
    if (rawArgs.contains("--help")) {
        QTextStream(stdout) << "Hyper Linux " << HYPER_LINUX_VERSION << "\n"
            "Usage: hyper-linux [--version] [--diagnose] [--backend-info]\n"
            "       hyper-linux [--preview] [--page 0..3] [--screenshot FILE]\n";
        return 0;
    }
    if (rawArgs.contains("--backend-info") || rawArgs.contains("--diagnose")) {
        QCoreApplication core(argc, argv);
        root = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
        auto command = backendCommand(rawArgs.contains("--backend-info") ? "--runtime-info" : "--diagnose");
        QProcess child; child.setProcessChannelMode(QProcess::ForwardedChannels);
        child.start(command.first, command.second);
        if (!child.waitForStarted(5000)) { QTextStream(stderr) << child.errorString() << '\n'; return 1; }
        if (!child.waitForFinished(15000)) { child.kill(); child.waitForFinished(); return 1; }
        return child.exitStatus() == QProcess::NormalExit ? child.exitCode() : 1;
    }
    QApplication app(argc, argv);
    app.setApplicationName("hyper-linux"); app.setOrganizationName("HyperLinux");
    app.setApplicationVersion(HYPER_LINUX_VERSION);
    root = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
    app.setFont(QFont("Sans Serif", 11));
    app.setWindowIcon(QIcon(headsetIllustration()));
    QStringList args = app.arguments();
    bool preview = args.contains("--preview") || args.contains("--screenshot");
    Window window(preview); window.show();
    if (args.contains("--page")) { int index = args.indexOf("--page"); if (index + 1 < args.size()) window.setPage(args[index + 1].toInt()); }
    if (args.contains("--screenshot")) {
        int index = args.indexOf("--screenshot");
        if (index + 1 >= args.size()) return 2;
        auto output = args[index + 1];
        QTimer::singleShot(150, &window, [&window, output] { QCoreApplication::exit(window.grab().save(output) ? 0 : 1); });
    }
    if (args.contains("--smoke-test")) QTimer::singleShot(700, &app, &QCoreApplication::quit);
    return app.exec();
}
