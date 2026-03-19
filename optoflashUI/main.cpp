#include <QtWidgets>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>

class ProtocolPreviewWidget : public QWidget {
public:
    explicit ProtocolPreviewWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(170);
    }

    void setData(const QVector<QPointF> &points, double totalMs, bool simplifiedFlicker) {
        points_ = points;
        totalMs_ = qMax(1.0, totalMs);
        simplifiedFlicker_ = simplifiedFlicker;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), palette().base());

        const QRect chartRect = rect().adjusted(44, 14, -14, -34);
        if (chartRect.width() < 20 || chartRect.height() < 20) {
            return;
        }

        painter.setPen(QPen(QColor(210, 210, 210), 1));
        painter.drawRect(chartRect);

        painter.setPen(QColor(120, 120, 120));
        painter.drawText(6, chartRect.top() + 5, "100%");
        painter.drawText(12, chartRect.bottom() + 5, "0%");

        // Draw X-axis ticks every 0.25 min (15 s) for denser time graduations.
        const double tickStepMs = 0.25 * 60.0 * 1000.0;
        const int tickCount = qMax(1, qCeil(totalMs_ / tickStepMs));
        const QFontMetrics fm = painter.fontMetrics();
        const int baselineY = height() - 10;
        const qreal tickSpacingPx = chartRect.width() / qreal(tickCount);
        const int labelEvery = qMax(1, qCeil(48.0 / qMax(1.0, tickSpacingPx)));

        for (int i = 0; i <= tickCount; ++i) {
            const double t = qMin(totalMs_, i * tickStepMs);
            const qreal x = chartRect.left() + (t / totalMs_) * chartRect.width();

            painter.setPen(QColor(235, 235, 235));
            painter.drawLine(QPointF(x, chartRect.top()), QPointF(x, chartRect.bottom()));

            painter.setPen(QColor(170, 170, 170));
            painter.drawLine(QPointF(x, chartRect.bottom()), QPointF(x, chartRect.bottom() + 4));

            if ((i % labelEvery) == 0 || i == tickCount) {
                const QString label = formatTickMinutes(t);
                const int textW = fm.horizontalAdvance(label);
                const int textX = qBound(chartRect.left(), int(x - (textW / 2)), chartRect.right() - textW);
                painter.setPen(QColor(120, 120, 120));
                painter.drawText(textX, baselineY, label);
            }
        }

        if (points_.isEmpty()) {
            painter.drawText(chartRect.adjusted(8, 8, -8, -8), Qt::AlignCenter, "No protocol to preview");
            return;
        }

        QPainterPath path;
        bool first = true;
        for (const QPointF &pt : points_) {
            const qreal x = chartRect.left() + (pt.x() / totalMs_) * chartRect.width();
            const qreal y = chartRect.bottom() - (pt.y() / 100.0) * chartRect.height();
            if (first) {
                path.moveTo(x, y);
                first = false;
            } else {
                path.lineTo(x, y);
            }
        }

        painter.setPen(QPen(QColor(25, 118, 210), 2));
        painter.drawPath(path);

        if (simplifiedFlicker_) {
            painter.setPen(QColor(130, 130, 130));
            painter.drawText(chartRect.adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignRight, "Flicker detail simplified");
        }
    }

private:
    static QString formatTickMinutes(double ms) {
        return QString::number(ms / 60000.0, 'f', 2);
    }

    QVector<QPointF> points_;
    double totalMs_ = 1.0;
    bool simplifiedFlicker_ = false;
};

class OptoDialog : public QDialog {
    Q_OBJECT
public:
    OptoDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("OptoFlash Control");
        setMinimumWidth(520);

        portCombo = new QComboBox(this);
        refreshButton = new QPushButton("Refresh", this);
        baudCombo = new QComboBox(this);
        baudCombo->addItem("9600");

        connect(refreshButton, &QPushButton::clicked, this, &OptoDialog::refreshPorts);

        QPushButton *connectButton = new QPushButton("Connect", this);
        connect(connectButton, &QPushButton::clicked, this, &OptoDialog::toggleConnection);

        statusLabel = new QLabel("Disconnected", this);

        QHBoxLayout *portRow = new QHBoxLayout();
        portRow->addWidget(new QLabel("Port", this));
        portRow->addWidget(portCombo, 1);
        portRow->addWidget(refreshButton);
        portRow->addSpacing(8);
        portRow->addWidget(new QLabel("Baud", this));
        portRow->addWidget(baudCombo);
        portRow->addWidget(connectButton);
        portRow->addWidget(statusLabel);

        // Parameters (single set)
        exptLengthMin = new QDoubleSpinBox(this);
        exptLengthMin->setRange(0.0, 600.0);
        exptLengthMin->setDecimals(2);
        exptLengthMin->setValue(5.0);

        brightnessPercent = new QSpinBox(this);
        brightnessPercent->setRange(0, 100);
        brightnessPercent->setValue(100);

        restMin = new QDoubleSpinBox(this);
        restMin->setRange(0.0, 600.0);
        restMin->setDecimals(2);
        restMin->setValue(0.0);

        stimPeriodMs = new QSpinBox(this);
        stimPeriodMs->setRange(1, 600000);
        stimPeriodMs->setValue(5000);

        cycleLengthMs = new QSpinBox(this);
        cycleLengthMs->setRange(1, 600000);
        cycleLengthMs->setValue(30000);

        continuousRadio = new QRadioButton("Continuous", this);
        flickerRadio = new QRadioButton("Flicker", this);
        continuousRadio->setChecked(true);
        connect(continuousRadio, &QRadioButton::toggled, this, &OptoDialog::updateFlicker);

        flickerOnMs = new QSpinBox(this);
        flickerOnMs->setRange(1, 600000);
        flickerOnMs->setValue(100);

        flickerOffMs = new QSpinBox(this);
        flickerOffMs->setRange(0, 600000);
        flickerOffMs->setValue(900);

        cyclePreview = new ProtocolPreviewWidget(this);
        cyclePreview->setFixedSize(170, 170);
        cyclePreview->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        protocolPreview = new ProtocolPreviewWidget(this);

        QFormLayout *paramForm = new QFormLayout();
        paramForm->addRow("Experiment Length (min)", exptLengthMin);
        paramForm->addRow("Power (%)", brightnessPercent);
        paramForm->addRow("Initial rest (min, optional)", restMin);
        paramForm->addRow("Cycle Length (ms)", cycleLengthMs);
        paramForm->addRow("Stimulation Period (ms)", stimPeriodMs);

        QHBoxLayout *modeRow = new QHBoxLayout();
        modeRow->addWidget(continuousRadio);
        modeRow->addWidget(flickerRadio);
        QWidget *modeWidget = new QWidget(this);
        modeWidget->setLayout(modeRow);
        paramForm->addRow("Mode", modeWidget);

        paramForm->addRow("LED on time (ms)", flickerOnMs);
        paramForm->addRow("LED off time (ms)", flickerOffMs);

        QGroupBox *paramBox = new QGroupBox("Stimulation Parameters", this);
        QVBoxLayout *paramBoxLayout = new QVBoxLayout();
        QHBoxLayout *previewRow = new QHBoxLayout();
        previewRow->addWidget(cyclePreview, 0, Qt::AlignTop | Qt::AlignLeft);
        previewRow->addWidget(protocolPreview, 1);
        paramBoxLayout->addLayout(previewRow);
        paramBoxLayout->addLayout(paramForm);
        paramBox->setLayout(paramBoxLayout);

        QPushButton *startButton = new QPushButton("Send START", this);
        QPushButton *stopButton = new QPushButton("Send STOP", this);
        QPushButton *statusButton = new QPushButton("Send STATUS", this);
        connect(startButton, &QPushButton::clicked, this, &OptoDialog::sendStart);
        connect(stopButton, &QPushButton::clicked, this, &OptoDialog::sendStop);
        connect(statusButton, &QPushButton::clicked, this, &OptoDialog::sendStatus);

        QHBoxLayout *buttonRow = new QHBoxLayout();
        buttonRow->addStretch(1);
        buttonRow->addWidget(startButton);
        buttonRow->addWidget(stopButton);
        buttonRow->addWidget(statusButton);

        logView = new QTextEdit(this);
        logView->setReadOnly(true);
        logView->setPlaceholderText("Serial log...");
        logView->setMinimumHeight(140);

        QVBoxLayout *mainLayout = new QVBoxLayout();
        mainLayout->addLayout(portRow);
        mainLayout->addSpacing(8);
        mainLayout->addWidget(paramBox);
        mainLayout->addLayout(buttonRow);
        mainLayout->addWidget(logView, 1);

        setLayout(mainLayout);

        serial = new QSerialPort(this);
        connect(serial, &QSerialPort::readyRead, this, &OptoDialog::readSerial);
        connect(exptLengthMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &OptoDialog::updateProtocolPreview);
        connect(brightnessPercent, qOverload<int>(&QSpinBox::valueChanged), this, &OptoDialog::updateProtocolPreview);
        connect(restMin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &OptoDialog::updateProtocolPreview);
        connect(cycleLengthMs, qOverload<int>(&QSpinBox::valueChanged), this, &OptoDialog::syncCycleLengthMinimum);
        connect(cycleLengthMs, qOverload<int>(&QSpinBox::valueChanged), this, &OptoDialog::updateProtocolPreview);
        connect(stimPeriodMs, qOverload<int>(&QSpinBox::valueChanged), this, &OptoDialog::syncCycleLengthMinimum);
        connect(stimPeriodMs, qOverload<int>(&QSpinBox::valueChanged), this, &OptoDialog::updateProtocolPreview);
        connect(flickerOnMs, qOverload<int>(&QSpinBox::valueChanged), this, &OptoDialog::updateProtocolPreview);
        connect(flickerOffMs, qOverload<int>(&QSpinBox::valueChanged), this, &OptoDialog::updateProtocolPreview);
        connect(continuousRadio, &QRadioButton::toggled, this, &OptoDialog::updateProtocolPreview);
        connect(flickerRadio, &QRadioButton::toggled, this, &OptoDialog::updateProtocolPreview);

        refreshPorts();
        updateFlicker();
        syncCycleLengthMinimum();
        updateProtocolPreview();
    }

private slots:
    void refreshPorts() {
        portCombo->clear();
        const auto ports = QSerialPortInfo::availablePorts();
        for (const QSerialPortInfo &info : ports) {
            portCombo->addItem(info.portName());
        }
        if (ports.isEmpty()) {
            portCombo->addItem("<no ports>");
        }
    }

    void toggleConnection() {
        if (serial->isOpen()) {
            serial->close();
            statusLabel->setText("Disconnected");
            logLine("Disconnected");
            return;
        }

        const QString portName = portCombo->currentText();
        if (portName.startsWith("<")) {
            logLine("No valid serial port selected");
            return;
        }

        serial->setPortName(portName);
        serial->setBaudRate(baudCombo->currentText().toInt());
        serial->setDataBits(QSerialPort::Data8);
        serial->setParity(QSerialPort::NoParity);
        serial->setStopBits(QSerialPort::OneStop);
        serial->setFlowControl(QSerialPort::NoFlowControl);

        if (!serial->open(QIODevice::ReadWrite)) {
            logLine(QString("Failed to open %1: %2").arg(portName, serial->errorString()));
            return;
        }

        statusLabel->setText("Connected");
        logLine(QString("Connected to %1").arg(portName));
    }

    void sendStart() {
        if (!ensureOpen()) {
            return;
        }

        const int burstDuration = stimPeriodMs->value();
        const bool flicker = flickerRadio->isChecked();
        const int onMs = flicker ? flickerOnMs->value() : burstDuration;
        const int offMs = flicker ? flickerOffMs->value() : 0;
        const int intervalMs = qMax(0, cycleLengthMs->value() - burstDuration);

        const QString cmd = QString("<START, %1, %2, %3, %4, %5, %6, %7, %8, %9>")
                                .arg(formatMin(exptLengthMin->value()))
                                .arg(formatMin(restMin->value()))
                                .arg(onMs) // legacy pulse width
                                .arg(formatHz(1.0)) // legacy Hz; overridden by burstDuration
                                .arg(onMs)
                                .arg(offMs)
                                .arg(burstDuration)
                                .arg(intervalMs)
                                .arg(brightnessPercent->value());

        writeLine(cmd);
    }

    void sendStop() {
        if (!ensureOpen()) {
            return;
        }
        writeLine("<STOP>");
    }

    void sendStatus() {
        if (!ensureOpen()) {
            return;
        }
        writeLine("<STATUS>");
    }

    void readSerial() {
        buffer.append(serial->readAll());
        int idx;
        while ((idx = buffer.indexOf('\n')) != -1) {
            QByteArray line = buffer.left(idx + 1);
            buffer.remove(0, idx + 1);
            logLine(QString::fromUtf8(line).trimmed());
        }
    }

private:
    bool ensureOpen() {
        if (serial->isOpen()) {
            return true;
        }
        logLine("Serial not connected. Click Connect first.");
        return false;
    }

    void writeLine(const QString &text) {
        const QByteArray bytes = text.toUtf8();
        serial->write(bytes);
        serial->write("\n");
        logLine("TX: " + text);
    }

    void logLine(const QString &line) {
        logView->append(line);
    }

    void updateFlicker() {
        const bool flicker = flickerRadio->isChecked();
        flickerOnMs->setEnabled(flicker);
        flickerOffMs->setEnabled(flicker);
        updateProtocolPreview();
    }

    void syncCycleLengthMinimum() {
        const int stimMs = stimPeriodMs->value();
        cycleLengthMs->setMinimum(stimMs);
        if (cycleLengthMs->value() < stimMs) {
            cycleLengthMs->setValue(stimMs);
        }
    }

    void updateProtocolPreview() {
        const double totalMs = exptLengthMin->value() * 60000.0;
        const int power = brightnessPercent->value();
        const double initialRestMs = qBound(0.0, restMin->value() * 60000.0, qMax(0.0, totalMs));
        const int burstMs = stimPeriodMs->value();
        const int cycleMs = cycleLengthMs->value();
        const int intervalMs = qMax(0, cycleLengthMs->value() - burstMs);
        const bool flicker = flickerRadio->isChecked();
        const int onMs = flicker ? flickerOnMs->value() : burstMs;
        const int offMs = flicker ? flickerOffMs->value() : 0;

        bool simplifiedFlicker = false;
        bool overflow = false;
        QVector<QPointF> points = buildProtocolPoints(totalMs, initialRestMs, power, burstMs, intervalMs, onMs, offMs, flicker, true, overflow);
        if (overflow) {
            simplifiedFlicker = true;
            points = buildProtocolPoints(totalMs, initialRestMs, power, burstMs, intervalMs, onMs, offMs, flicker, false, overflow);
        }

        protocolPreview->setData(points, totalMs, simplifiedFlicker);

        bool cycleSimplifiedFlicker = false;
        bool cycleOverflow = false;
        QVector<QPointF> cyclePoints = buildProtocolPoints(cycleMs, 0.0, power, burstMs, intervalMs, onMs, offMs, flicker, true, cycleOverflow);
        if (cycleOverflow) {
            cycleSimplifiedFlicker = true;
            cyclePoints = buildProtocolPoints(cycleMs, 0.0, power, burstMs, intervalMs, onMs, offMs, flicker, false, cycleOverflow);
        }
        cyclePreview->setData(cyclePoints, cycleMs, cycleSimplifiedFlicker);
    }

    static QString formatMin(double v) {
        return QString::number(v, 'f', 2);
    }

    static QString formatHz(double v) {
        return QString::number(v, 'f', 4);
    }

    static QVector<QPointF> buildProtocolPoints(
        double totalMs,
        double initialRestMs,
        int power,
        int burstMs,
        int intervalMs,
        int onMs,
        int offMs,
        bool flicker,
        bool includeFlickerDetail,
        bool &overflow) {
        overflow = false;
        QVector<QPointF> points;
        if (totalMs <= 0.0) {
            return points;
        }

        const int maxPoints = 3000;
        auto addTransition = [&](double t, double level) {
            t = qBound(0.0, t, totalMs);
            if (points.isEmpty()) {
                points.append(QPointF(t, level));
                return;
            }

            const double prevLevel = points.last().y();
            if (!qFuzzyCompare(prevLevel + 1.0, level + 1.0)) {
                points.append(QPointF(t, prevLevel));
                points.append(QPointF(t, level));
            } else if (!qFuzzyCompare(points.last().x() + 1.0, t + 1.0)) {
                points.append(QPointF(t, level));
            }

            if (points.size() > maxPoints) {
                overflow = true;
            }
        };

        double t = 0.0;
        addTransition(0.0, 0.0);
        t = initialRestMs;
        addTransition(t, 0.0);

        while (t < totalMs && !overflow) {
            const double stimStart = t;
            const double stimEnd = qMin(totalMs, stimStart + burstMs);

            if (flicker && includeFlickerDetail && onMs > 0 && (onMs + offMs) > 0) {
                double pulseT = stimStart;
                while (pulseT < stimEnd && !overflow) {
                    const double onEnd = qMin(stimEnd, pulseT + onMs);
                    addTransition(pulseT, power);
                    addTransition(onEnd, 0.0);
                    pulseT = onEnd;

                    if (pulseT >= stimEnd || offMs <= 0) {
                        continue;
                    }
                    pulseT = qMin(stimEnd, pulseT + offMs);
                    addTransition(pulseT, 0.0);
                }
            } else {
                addTransition(stimStart, power);
                addTransition(stimEnd, 0.0);
            }

            t = stimEnd;
            if (t >= totalMs) {
                break;
            }

            const double restEnd = qMin(totalMs, t + intervalMs);
            addTransition(restEnd, 0.0);
            t = restEnd;
        }

        addTransition(totalMs, 0.0);
        return points;
    }

    QComboBox *portCombo = nullptr;
    QPushButton *refreshButton = nullptr;
    QComboBox *baudCombo = nullptr;
    QLabel *statusLabel = nullptr;

    QDoubleSpinBox *exptLengthMin = nullptr;
    QDoubleSpinBox *restMin = nullptr;
    QSpinBox *brightnessPercent = nullptr;

    QSpinBox *stimPeriodMs = nullptr;
    QSpinBox *cycleLengthMs = nullptr;
    QRadioButton *continuousRadio = nullptr;
    QRadioButton *flickerRadio = nullptr;
    QSpinBox *flickerOnMs = nullptr;
    QSpinBox *flickerOffMs = nullptr;
    ProtocolPreviewWidget *cyclePreview = nullptr;
    ProtocolPreviewWidget *protocolPreview = nullptr;

    QTextEdit *logView = nullptr;

    QSerialPort *serial = nullptr;
    QByteArray buffer;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    OptoDialog dlg;
    dlg.show();
    return app.exec();
}

#include "main.moc"
