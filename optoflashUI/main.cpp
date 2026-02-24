#include <QtWidgets>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>

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

        // Mode selection
        steadyRadio = new QRadioButton("Steady (single pulse per cycle)", this);
        burstRadio = new QRadioButton("Burst (sub-pulses inside cycle)", this);
        steadyRadio->setChecked(true);

        connect(steadyRadio, &QRadioButton::toggled, this, &OptoDialog::updateMode);

        // Common parameters
        exptLengthMin = new QDoubleSpinBox(this);
        exptLengthMin->setRange(0.0, 600.0);
        exptLengthMin->setDecimals(2);
        exptLengthMin->setValue(5.0);

        restMin = new QDoubleSpinBox(this);
        restMin->setRange(0.0, 600.0);
        restMin->setDecimals(2);
        restMin->setValue(0.0);

        pulseWidthMs = new QSpinBox(this);
        pulseWidthMs->setRange(1, 600000);
        pulseWidthMs->setValue(5000);

        frequencyHz = new QDoubleSpinBox(this);
        frequencyHz->setRange(0.0001, 1000.0);
        frequencyHz->setDecimals(4);
        frequencyHz->setValue(0.0333); // 30s period

        brightnessPercent = new QSpinBox(this);
        brightnessPercent->setRange(0, 100);
        brightnessPercent->setValue(100);

        QFormLayout *commonForm = new QFormLayout();
        commonForm->addRow("Experiment length (min)", exptLengthMin);
        commonForm->addRow("Initial rest (min, optional)", restMin);
        commonForm->addRow("Stimulation time (ms, steady on-time)", pulseWidthMs);
        commonForm->addRow("Steady cycle rate (Hz)", frequencyHz);
        commonForm->addRow("Brightness (%)", brightnessPercent);

        QGroupBox *commonBox = new QGroupBox("Common Parameters", this);
        commonBox->setLayout(commonForm);

        // Burst parameters
        burstOnMs = new QSpinBox(this);
        burstOnMs->setRange(1, 600000);
        burstOnMs->setValue(100);

        burstOffMs = new QSpinBox(this);
        burstOffMs->setRange(0, 600000);
        burstOffMs->setValue(900);

        burstDurationMs = new QSpinBox(this);
        burstDurationMs->setRange(1, 600000);
        burstDurationMs->setValue(5000);

        restBetweenMs = new QSpinBox(this);
        restBetweenMs->setRange(0, 600000);
        restBetweenMs->setValue(25000);

        QFormLayout *burstForm = new QFormLayout();
        burstForm->addRow("Pulse width (ms, ON)", burstOnMs);
        burstForm->addRow("Pulse gap (ms, OFF)", burstOffMs);
        burstForm->addRow("Burst duration (ms)", burstDurationMs);
        burstForm->addRow("Rest between bursts (ms)", restBetweenMs);

        burstBox = new QGroupBox("Burst Parameters", this);
        burstBox->setLayout(burstForm);

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
        mainLayout->addWidget(steadyRadio);
        mainLayout->addWidget(burstRadio);
        mainLayout->addWidget(commonBox);
        mainLayout->addWidget(burstBox);
        mainLayout->addLayout(buttonRow);
        mainLayout->addWidget(logView, 1);

        setLayout(mainLayout);

        serial = new QSerialPort(this);
        connect(serial, &QSerialPort::readyRead, this, &OptoDialog::readSerial);

        refreshPorts();
        updateMode();
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

    void updateMode() {
        const bool burst = burstRadio->isChecked();
        burstBox->setEnabled(burst);
    }

    void sendStart() {
        if (!ensureOpen()) {
            return;
        }

        QString cmd;
        if (burstRadio->isChecked()) {
            cmd = QString("<START, %1, %2, %3, %4, %5, %6, %7, %8, %9>")
                      .arg(formatMin(exptLengthMin->value()))
                      .arg(formatMin(restMin->value()))
                      .arg(pulseWidthMs->value())
                      .arg(formatHz(frequencyHz->value()))
                      .arg(burstOnMs->value())
                      .arg(burstOffMs->value())
                      .arg(burstDurationMs->value())
                      .arg(restBetweenMs->value())
                      .arg(brightnessPercent->value());
        } else {
            cmd = QString("<START, %1, %2, %3, %4, %5>")
                      .arg(formatMin(exptLengthMin->value()))
                      .arg(formatMin(restMin->value()))
                      .arg(pulseWidthMs->value())
                      .arg(formatHz(frequencyHz->value()))
                      .arg(brightnessPercent->value());
        }

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

    static QString formatMin(double v) {
        return QString::number(v, 'f', 2);
    }

    static QString formatHz(double v) {
        return QString::number(v, 'f', 4);
    }

    QComboBox *portCombo = nullptr;
    QPushButton *refreshButton = nullptr;
    QComboBox *baudCombo = nullptr;
    QLabel *statusLabel = nullptr;

    QRadioButton *steadyRadio = nullptr;
    QRadioButton *burstRadio = nullptr;

    QDoubleSpinBox *exptLengthMin = nullptr;
    QDoubleSpinBox *restMin = nullptr;
    QSpinBox *pulseWidthMs = nullptr;
    QDoubleSpinBox *frequencyHz = nullptr;
    QSpinBox *brightnessPercent = nullptr;

    QGroupBox *burstBox = nullptr;
    QSpinBox *burstOnMs = nullptr;
    QSpinBox *burstOffMs = nullptr;
    QSpinBox *burstDurationMs = nullptr;
    QSpinBox *restBetweenMs = nullptr;

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
