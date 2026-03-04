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

        restIntervalMs = new QSpinBox(this);
        restIntervalMs->setRange(0, 600000);
        restIntervalMs->setValue(25000);

        QFormLayout *paramForm = new QFormLayout();
        paramForm->addRow("Experiment Length (min)", exptLengthMin);
        paramForm->addRow("Power (%)", brightnessPercent);
        paramForm->addRow("Initial rest (min, optional)", restMin);
        paramForm->addRow("Stimulation Period (ms)", stimPeriodMs);

        QHBoxLayout *modeRow = new QHBoxLayout();
        modeRow->addWidget(continuousRadio);
        modeRow->addWidget(flickerRadio);
        QWidget *modeWidget = new QWidget(this);
        modeWidget->setLayout(modeRow);
        paramForm->addRow("Mode", modeWidget);

        paramForm->addRow("LED on time (ms)", flickerOnMs);
        paramForm->addRow("LED off time (ms)", flickerOffMs);
        paramForm->addRow("Rest interval (ms)", restIntervalMs);

        QGroupBox *paramBox = new QGroupBox("Stimulation Parameters", this);
        paramBox->setLayout(paramForm);

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

        refreshPorts();
        updateFlicker();
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
        const int intervalMs = restIntervalMs->value();

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

    QDoubleSpinBox *exptLengthMin = nullptr;
    QDoubleSpinBox *restMin = nullptr;
    QSpinBox *brightnessPercent = nullptr;

    QSpinBox *stimPeriodMs = nullptr;
    QRadioButton *continuousRadio = nullptr;
    QRadioButton *flickerRadio = nullptr;
    QSpinBox *flickerOnMs = nullptr;
    QSpinBox *flickerOffMs = nullptr;
    QSpinBox *restIntervalMs = nullptr;

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
