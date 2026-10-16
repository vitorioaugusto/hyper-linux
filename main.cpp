#include <QtWidgets>
#include <cmath>
#include <functional>
#include <array>
#include <optional>

static QString root;
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
            if (!preview) showError("O serviço local não está em execução.");
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
        if (!box->count()) box->addItem("Dispositivo não encontrado", "");
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
        audioNotice->setText(available ? "Áudio conectado ao PipeWire" : "Atualize os dispositivos com o dongle conectado.");
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
        connection->setText(ready ? "●  CONECTADO" : hasConnection ? "○  FONE DESLIGADO" : "○  AGUARDANDO FONE");
        status->setText(ready ? "Seu Cloud III, no Linux." : "Conecte o dongle e ligue o fone.");
        battery->setText(state["battery"].isDouble() ? QString::number(state["battery"].toInt()) + "%" : "—");
        if (state["charging"].isDouble() && state["charging"].toInt() == 1) battery->setText(battery->text() + " · carregando");
        if (state["charging"].isDouble() && state["charging"].toInt() == 2) battery->setText(battery->text() + " · carga completa");
        mute->setEnabled(ready || !target(sources).isEmpty()); sidetone->setEnabled(ready); sleep->setEnabled(ready);
        if (target(sources).isEmpty()) mute->setChecked(state["mute"].toBool());
        sidetone->setChecked(state["sidetone"].toBool());
        sidetone->setText(state["sidetone"].isBool() ? "Ouvir minha voz no fone" : "Ouvir minha voz no fone · estado desconhecido");
        if (state["sleep"].isDouble()) sleep->setValue(state["sleep"].toInt());
        auto dev = state["device"].toObject();
        usbDetail->setText(state["usb_ready"].toBool() ? QString("%1 · %2 bytes · %3").arg(
            state["transport"].toString()).arg(state["packet_size"].toInt()).arg(dev["path"].toString()) : "Comunicação USB ainda não confirmada");
        bool changedMute = state["mute"].isBool() && (!lastHardwareMute.has_value() || *lastHardwareMute != state["mute"].toBool());
        if (state["mute"].isBool()) lastHardwareMute = state["mute"].toBool(); else lastHardwareMute.reset();
        loading = false;
        if (changedMute && !target(sources).isEmpty())
            send({{"action", "audio_devices"}}, [this](QJsonObject r) { if (r["ok"].toBool()) updateAudio(r["data"].toObject()); });
    }
    void setHardware(QString name, QJsonValue value) {
        if (loading) return;
        notice->setText("Enviando comando e aguardando confirmação do fone…"); notice->show();
        send({{"action", "set"}, {"name", name}, {"value", value}}, [this](QJsonObject r) {
            if (r["ok"].toBool()) notice->setText(QString("Configuração confirmada pelo fone · %1 ms").arg(r["data"].toObject()["elapsed_ms"].toInt()));
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
                eqNotice->setText("Confira as bandas: frequência de 20 a 20000 Hz e Q de 0.1 a 12. Use ponto como separador decimal.");
                return;
            }
        }
        eqNotice->setText("Iniciando equalizador…");
        send({{"action", "eq"}, {"enabled", true}, {"sink", target(sinks)}, {"bands", bands()}}, [this](QJsonObject r) {
            eqActive = r["ok"].toBool(); eqStop->setEnabled(eqActive);
            eqNotice->setText(eqActive ? "Ativo. Selecione “Hyper Linux · Equalizador” como saída nas configurações de som. Fechar o app desativa o efeito."
                                        : r["error"].toString());
        });
    }
    void loadPreset(QJsonObject profile) {
        auto values = profile["bands"].toArray();
        if (values.size() != 10) { showError("Perfil inválido: são necessárias 10 bandas."); return; }
        for (auto value : values) {
            auto b = value.toObject();
            if (!b["frequency"].isDouble() || !b["gain"].isDouble() || !b["q"].isDouble()
                || !std::isfinite(b["frequency"].toDouble()) || b["frequency"].toDouble() < 20 || b["frequency"].toDouble() > 20000
                || !std::isfinite(b["gain"].toDouble()) || std::abs(b["gain"].toDouble()) > 12
                || !std::isfinite(b["q"].toDouble()) || b["q"].toDouble() < .1 || b["q"].toDouble() > 12) {
                showError("Perfil inválido: frequência, ganho ou Q fora do intervalo."); return;
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
        QVBoxLayout *visual; auto *product = card("FEITO PARA JOGAR.", "Cloud III Wireless · 2,4 GHz", &visual);
        product->setMinimumWidth(310);
        image = new QLabel; image->setAlignment(Qt::AlignCenter);
        QPixmap pix = headsetIllustration();
        image->setPixmap(pix.scaled(330, 310, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        visual->addWidget(image, 1);
        visual->addWidget(text("BATERIA", "eyebrow")); battery = text("—", "battery"); visual->addWidget(battery);
        visual->addWidget(text("Nível informado pelo fone. Sem estimativas.", "muted"));
        row->addWidget(product, 5);
        auto *controls = new QVBoxLayout; controls->setSpacing(18);
        QVBoxLayout *mic; auto *micCard = card("Microfone", "Um único controle para o mute integrado ao seu sistema.", &mic);
        mute = new QCheckBox("Silenciar microfone"); mute->setEnabled(false); mic->addWidget(mute);
        mic->addWidget(text("O estado acompanha o botão físico do fone e as configurações de som do Linux.", "muted"));
        connect(mute, &QCheckBox::clicked, this, [this](bool b) {
            if (!target(sources).isEmpty()) audioSet("source", {{"mute", b}});
            else setHardware("mute", b);
        });
        controls->addWidget(micCard);
        QVBoxLayout *side; auto *sideCard = card("Retorno do microfone", "Sidetone do próprio fone, sem passar pelo computador.", &side);
        sidetone = new QCheckBox("Ouvir minha voz no fone"); sidetone->setEnabled(false); side->addWidget(sidetone);
        side->addWidget(text("Se o retorno não for audível, teste o monitoramento por software na aba Áudio.", "muted"));
        connect(sidetone, &QCheckBox::clicked, this, [this](bool b) { setHardware("sidetone", b); });
        controls->addWidget(sideCard);
        QVBoxLayout *power; auto *powerCard = card("Desligamento automático", "Tempo de inatividade. 0 mantém o fone ligado.", &power);
        auto *powerRow = new QHBoxLayout; sleep = new QSpinBox; sleep->setRange(0, 255); sleep->setSuffix(" min"); sleep->setSpecialValueText("Nunca"); sleep->setEnabled(false);
        auto *save = new QPushButton("Aplicar"); powerRow->addWidget(sleep); powerRow->addWidget(save); power->addLayout(powerRow);
        connect(save, &QPushButton::clicked, this, [this] { if (sleep->isEnabled()) setHardware("sleep", sleep->value()); });
        controls->addWidget(powerCard); row->addLayout(controls, 6); outer->addLayout(row);
        usbDetail = text("Comunicação USB ainda não confirmada", "muted"); outer->addWidget(usbDetail); outer->addStretch();
        return page;
    }
    QWidget *audioPage() {
        auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(18);
        QVBoxLayout *devices; auto *deviceCard = card("Dispositivos de áudio", "Selecione a entrada e a saída físicas do HyperX.", &devices);
        auto *grid = new QGridLayout; sinks = new QComboBox; sources = new QComboBox;
        sinks->setMinimumContentsLength(15); sources->setMinimumContentsLength(15);
        sinks->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); sources->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        grid->addWidget(text("Saída"), 0, 0); grid->addWidget(sinks, 0, 1); grid->addWidget(text("Microfone"), 1, 0); grid->addWidget(sources, 1, 1);
        outputVolume = new QSlider(Qt::Horizontal); outputVolume->setRange(0, 100); outputVolume->setEnabled(false);
        inputVolume = new QSlider(Qt::Horizontal); inputVolume->setRange(0, 100); inputVolume->setEnabled(false);
        grid->addWidget(text("Volume do fone"), 2, 0); grid->addWidget(outputVolume, 2, 1);
        grid->addWidget(text("Ganho do microfone"), 3, 0); grid->addWidget(inputVolume, 3, 1);
        devices->addLayout(grid); audioNotice = text("Atualize os dispositivos com o dongle conectado.", "muted"); devices->addWidget(audioNotice);
        auto *refresh = new QPushButton("Atualizar dispositivos"); devices->addWidget(refresh, 0, Qt::AlignLeft);
        connect(refresh, &QPushButton::clicked, this, [this] { refreshAudio(); });
        connect(outputVolume, &QSlider::sliderReleased, this, [this] { audioSet("sink", {{"volume", outputVolume->value()}}); });
        connect(inputVolume, &QSlider::sliderReleased, this, [this] { audioSet("source", {{"volume", inputVolume->value()}}); });
        layout->addWidget(deviceCard);
        QVBoxLayout *monitor; auto *monitorCard = card("Ouvir meu microfone", "Monitoramento local pelo PipeWire. Pode haver um pequeno atraso de áudio.", &monitor);
        loopback = new QCheckBox("Ativar retorno por software"); loopback->setEnabled(false); monitor->addWidget(loopback);
        auto *volumeRow = new QHBoxLayout; volumeRow->addWidget(text("Volume do retorno")); loopVolume = new QSpinBox; loopVolume->setRange(0, 100); loopVolume->setValue(25); loopVolume->setSuffix("%"); volumeRow->addWidget(loopVolume); volumeRow->addStretch(); monitor->addLayout(volumeRow);
        loopNotice = text("Comece com volume baixo. Use apenas um retorno por vez para evitar eco.", "muted"); monitor->addWidget(loopNotice);
        auto applyLoop = [this] {
            if (loading) return;
            bool enabled = loopback->isChecked(); loopNotice->setText("Aplicando monitoramento…");
            send({{"action", "loopback"}, {"enabled", enabled}, {"source", target(sources)}, {"sink", target(sinks)}, {"volume", loopVolume->value()}},
                [this, enabled](QJsonObject r) {
                    loading = true; loopback->setChecked(r["ok"].toBool() && enabled); loading = false;
                    loopNotice->setText(r["ok"].toBool() ? enabled ? "Monitoramento local ativo. Fechar o app encerra o retorno." : "Monitoramento desligado."
                                                                  : r["error"].toString());
                });
        };
        connect(loopback, &QCheckBox::clicked, this, applyLoop);
        connect(loopVolume, &QSpinBox::editingFinished, this, [this, applyLoop] { if (loopback->isChecked()) applyLoop(); });
        layout->addWidget(monitorCard);
        QVBoxLayout *compat; auto *compatCard = card("Recursos do NGENUITY", "O motor proprietário de áudio do Windows ainda não foi portado.", &compat);
        compat->addWidget(text("A equalização nativa está na próxima aba. DTS/SONAMI, supressão de voz por IA e atualização de firmware não estão disponíveis nesta versão.", "muted"));
        layout->addWidget(compatCard); layout->addStretch();
        return page;
    }
    QWidget *eqPage() {
        auto *page = new QWidget; auto *outer = new QVBoxLayout(page); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(18);
        QVBoxLayout *content; auto *frame = card("Equalizador", "10 bandas com frequência e Q ajustáveis. O processamento acontece no Linux.", &content);
        auto *top = new QHBoxLayout; presets = new QComboBox; presets->addItems({"Flat", "Graves", "Voz", "Personalizado"});
        top->addWidget(text("Perfil")); top->addWidget(presets); top->addStretch();
        auto *save = new QPushButton("Salvar perfil"); auto *load = new QPushButton("Abrir perfil"); top->addWidget(load); top->addWidget(save); content->addLayout(top);
        curve = new Curve; content->addWidget(curve);
        auto *grid = new QGridLayout; grid->setHorizontalSpacing(9);
        grid->addWidget(text("Hz", "muted"), 2, 0); grid->addWidget(text("Q", "muted"), 3, 0);
        for (int i = 0; i < 10; ++i) {
            auto *gain = new QLabel("0.0 dB"); gain->setAlignment(Qt::AlignCenter); gain->setObjectName("muted"); grid->addWidget(gain, 0, i + 1);
            auto *slider = new QSlider(Qt::Vertical); slider->setRange(-120, 120); slider->setMinimumHeight(110); bandSliders[i] = slider; grid->addWidget(slider, 1, i + 1, Qt::AlignHCenter);
            auto *freq = new QLineEdit(QString::number(frequencies[i]));
            auto *freqValidator = new QDoubleValidator(20, 20000, 0, freq); freqValidator->setLocale(QLocale::c()); freqValidator->setNotation(QDoubleValidator::StandardNotation); freq->setValidator(freqValidator);
            freq->setFixedWidth(64); freq->setAlignment(Qt::AlignCenter); freq->setToolTip("Frequência em Hz (20–20000)"); bandFreqs[i] = freq; grid->addWidget(freq, 2, i + 1, Qt::AlignHCenter);
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
        content->addWidget(text("A curva mostra o ganho por banda. Os filtros usam as frequências e valores de Q acima. A margem automática reduz o volume para evitar distorção.", "muted"));
        auto *actions = new QHBoxLayout; eqApply = new QPushButton("Ativar / aplicar"); eqApply->setObjectName("primary"); eqApply->setEnabled(false);
        eqStop = new QPushButton("Desativar"); eqStop->setEnabled(false); actions->addWidget(eqApply); actions->addWidget(eqStop); actions->addStretch(); content->addLayout(actions);
        eqNotice = text("Selecione a saída física na aba Áudio. Ao ativar, escolha a saída do equalizador nas configurações de som.", "muted"); content->addWidget(eqNotice);
        outer->addWidget(frame); outer->addStretch();
        connect(eqApply, &QPushButton::clicked, this, [this] { applyEq(); });
        connect(eqStop, &QPushButton::clicked, this, [this] {
            eqDebounce.stop(); send({{"action", "eq"}, {"enabled", false}}, [this](QJsonObject r) {
                if (r["ok"].toBool()) { eqActive = false; eqStop->setEnabled(false); eqNotice->setText("Equalizador desligado."); }
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
            auto name = QFileDialog::getSaveFileName(this, "Salvar perfil", QDir::homePath() + "/cloud-iii.json", "Perfil (*.json)");
            if (name.isEmpty()) return;
            QSaveFile file(name); if (!file.open(QIODevice::WriteOnly)) { showError(file.errorString()); return; }
            file.write(QJsonDocument(QJsonObject{{"format", "hyper-linux-eq-v1"}, {"bands", bands()}}).toJson());
            if (!file.commit()) showError(file.errorString());
        });
        connect(load, &QPushButton::clicked, this, [this] {
            auto name = QFileDialog::getOpenFileName(this, "Abrir perfil", QDir::homePath(), "Perfil (*.json)"); if (name.isEmpty()) return;
            QFile file(name); if (!file.open(QIODevice::ReadOnly) || file.size() > 100000) { showError("Não foi possível ler esse perfil."); return; }
            QJsonParseError error; auto document = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject() || document.object()["format"].toString() != "hyper-linux-eq-v1") { showError("Formato de perfil inválido."); return; }
            loadPreset(document.object()); presets->setCurrentIndex(3);
        });
        eqDebounce.setSingleShot(true); eqDebounce.setInterval(700);
        connect(&eqDebounce, &QTimer::timeout, this, [this] { if (eqActive) applyEq(); });
        return page;
    }
    QWidget *diagnosticPage() {
        auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(text("Diagnóstico", "cardTitle"));
        layout->addWidget(text("Os logs registram apenas comandos USB e estado do dispositivo; não gravam o áudio do microfone.", "muted"));
        diagnostics = new QPlainTextEdit; diagnostics->setReadOnly(true); layout->addWidget(diagnostics, 1);
        auto *buttons = new QHBoxLayout; auto *inspect = new QPushButton("Gerar diagnóstico"); auto *logs = new QPushButton("Abrir pasta dos logs");
        buttons->addWidget(inspect); buttons->addWidget(logs); buttons->addStretch(); layout->addLayout(buttons);
        connect(inspect, &QPushButton::clicked, this, [this] {
            send({{"action", "diagnose"}}, [this](QJsonObject r) { diagnostics->setPlainText(QString::fromUtf8(QJsonDocument(r).toJson())); });
        });
        connect(logs, &QPushButton::clicked, this, [] {
            QString folder = qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + "/.local/state") + "/hyper-linux";
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        });
        layout->addWidget(text("Versão experimental independente. Protocolo reconstruído do NGENUITY 3.1.0. A implementação de hardware ainda precisa de validação no aparelho.", "muted"));
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
            if (obj["name"] == "loop") { loopback->setChecked(false); loopNotice->setText("Monitoramento encerrado pelo PipeWire. Consulte os logs."); }
            if (obj["name"] == "eq") { eqActive = false; eqStop->setEnabled(false); eqNotice->setText("Equalizador encerrado pelo PipeWire. Consulte os logs."); }
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
        QStringList names{"Dispositivo", "Áudio", "Equalizador", "Diagnóstico"};
        pages = new QStackedWidget;
        for (int i = 0; i < names.size(); ++i) {
            auto *button = new QPushButton(names[i]); button->setCheckable(true); button->setObjectName("nav"); group->addButton(button, i); nav->addWidget(button);
            if (i == 0) button->setChecked(true);
        }
        nav->addStretch(); nav->addWidget(text("CLOUD III\nWIRELESS", "eyebrow")); nav->addSpacing(8); nav->addWidget(text("Edição independente\nExperimental · 0.1", "muted")); shell->addWidget(sidebar);
        auto *main = new QVBoxLayout; main->setContentsMargins(28, 26, 28, 22); main->setSpacing(14);
        auto *header = new QHBoxLayout; auto *heading = new QVBoxLayout;
        heading->addWidget(text("CLOUD III WIRELESS", "heading")); status = text("Conecte o dongle e ligue o fone.", "muted"); heading->addWidget(status); header->addLayout(heading, 1);
        auto *right = new QVBoxLayout; connection = text("○  AGUARDANDO FONE", "eyebrow"); connection->setMinimumWidth(175); right->addWidget(connection);
        auto *refresh = new QPushButton("Reconectar"); right->addWidget(refresh); header->addLayout(right); main->addLayout(header);
        notice = text("", "notice"); notice->setTextInteractionFlags(Qt::TextSelectableByMouse); notice->hide(); main->addWidget(notice);
        pages->addWidget(devicePage()); pages->addWidget(audioPage()); pages->addWidget(eqPage()); pages->addWidget(diagnosticPage());
        auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(pages); main->addWidget(scroll, 1);
        main->addWidget(text("HYPER LINUX  /  CONTROLE LOCAL  /  CLOUD III WIRELESS", "footer")); shell->addLayout(main, 1);
        connect(group, &QButtonGroup::idClicked, pages, &QStackedWidget::setCurrentIndex);
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
        if (preview) { status->setText("Prévia da interface · sem conexão com hardware"); return; }
        connect(&backend, &QProcess::readyReadStandardOutput, this, [this] {
            buffer += backend.readAllStandardOutput();
            while (buffer.contains('\n')) {
                int n = buffer.indexOf('\n'); auto line = buffer.left(n); buffer.remove(0, n + 1);
                QJsonParseError error; auto doc = QJsonDocument::fromJson(line, &error);
                if (error.error == QJsonParseError::NoError && doc.isObject()) message(doc.object());
            }
        });
        connect(&backend, &QProcess::readyReadStandardError, this, [this] { diagnostics->appendPlainText(QString::fromUtf8(backend.readAllStandardError())); });
        connect(&backend, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { showError("Falha ao iniciar serviço local: " + backend.errorString()); });
        connect(&backend, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            updateState({}); pending.clear(); loopback->setChecked(false); eqActive = false;
            showError("O serviço local encerrou. Feche e abra o aplicativo novamente.");
        });
        connect(&backend, &QProcess::started, this, [this] { send({{"action", "refresh"}}); refreshAudio(); });
        backend.start("/usr/bin/python3", {root + "/backend.py", "--server"});
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
    QApplication app(argc, argv);
    app.setApplicationName("hyper-linux"); app.setOrganizationName("HyperLinux");
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
