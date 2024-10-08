#include "GNSS_desktop_application.h"
#include "CSVUtils.h"
#include "NetworkUtils.h"
#include "SerialPortUtils.h"  
#include "CustomWebEnginePage.h"
// user include begin
#include <QVBoxLayout>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWebEngineCore/QWebEnginePage>
#include <QTimer>
#include <QMessageBox>
#include <QDebug>
#include <QSerialPort>
#include <QSerialPortInfo>
#include "GPSData.h"
#include <QProcess>
#include <QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QCloseEvent>
#include <QIcon>
// user include end


GNSS_desktop_application::GNSS_desktop_application(QWidget* parent)
    : QMainWindow(parent), serial(new QSerialPort(this)), showingRealTimeLocation(false), recordingData(false)
{
    ui.setupUi(this);
    QString iconPath = QCoreApplication::applicationDirPath() + "/GPSViewer.ico";
    setWindowIcon(QIcon(iconPath));
    baudRate = 921600;
    comPort = "COM55";
    fileLocation = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    updateFrequency = 1;
    ui.baudRateLineEdit->setText(QString::number(baudRate));
    ui.comPortLineEdit->setText(comPort);
    ui.fileLocationLineEdit->setText(fileLocation);
    ui.updateFrequencyComboBox->setCurrentIndex(updateFrequency - 1);

    webView = new QWebEngineView(this);
    webView->setPage(new CustomWebEnginePage(webView));  

    webView->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    webView->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    webView->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

    QString filePath = QCoreApplication::applicationDirPath() + "/google_maps.html";
    webView->setUrl(QUrl::fromLocalFile(filePath));
    QWidget* mapWidget = ui.mapWidget;
    QVBoxLayout* mapLayout = new QVBoxLayout(mapWidget);
    mapLayout->addWidget(webView);

    ui.showRealTimeButton->setEnabled(false);
    ui.toggleRecordingButton->setEnabled(false);

    connect(ui.browseButton, &QPushButton::clicked, this, &GNSS_desktop_application::onBrowseButtonClicked);
    connect(ui.connectButton, &QPushButton::clicked, this, &GNSS_desktop_application::onConnectButtonClicked);
    connect(ui.showRealTimeButton, &QPushButton::clicked, this, &GNSS_desktop_application::onShowRealTimeButtonClicked);
    connect(ui.toggleRecordingButton, &QPushButton::clicked, this, &GNSS_desktop_application::onToggleRecordingButtonClicked);
    connect(serial, &QSerialPort::readyRead, this, [this]() {
        SerialPortUtils::readSerialData(serial, gnssData, updateFrequency, showingRealTimeLocation, recordingData, csvFile, webView, ui);
        });
    connect(ui.loadHistoricalButton, &QPushButton::clicked, this, &GNSS_desktop_application::onLoadHistoricalDataButtonClicked);
    connect(ui.refreshMapButton, &QPushButton::clicked, this, &GNSS_desktop_application::onClearMapButtonClicked);
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);



    QTimer* wifiCheckTimer = new QTimer(this);
    connect(wifiCheckTimer, &QTimer::timeout, this, [this]() {
        NetworkUtils::checkInternetQuality(webView, ui);
        });
    wifiCheckTimer->start(2000);
}

GNSS_desktop_application::~GNSS_desktop_application()
{
    if (serial->isOpen())
        serial->close();
    if (csvFile.isOpen())
        csvFile.close();
}


void GNSS_desktop_application::onBrowseButtonClicked()
{
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);

    QString folderName = QFileDialog::getExistingDirectory(this, tr("Select Folder"), defaultPath, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (folderName.isEmpty() || folderName == defaultPath) {

        QMessageBox::information(this, tr("No Folder Selected"), tr("No folder has been selected, using default folder location: %1").arg(defaultPath));

        fileLocation = defaultPath;
        ui.fileLocationLineEdit->setText(fileLocation);
    }
    else {

        ui.fileLocationLineEdit->setText(folderName);
        fileLocation = folderName;
    }
}


void GNSS_desktop_application::onConnectButtonClicked()
{
    if (serial->isOpen()) {
        serial->close();
        ui.connectButton->setText("Connect");
        ui.baudRateLineEdit->setEnabled(true);
        ui.comPortLineEdit->setEnabled(true);
        ui.fileLocationLineEdit->setEnabled(true);
        ui.fileNameLineEdit->setEnabled(true);
        ui.browseButton->setEnabled(true);     
        ui.updateFrequencyComboBox->setEnabled(true);

        if (recordingData) {
            recordingData = false;
            ui.toggleRecordingButton->setText("Start Recording");
            if (csvFile.isOpen()) {
                csvFile.close();
            }
            ui.mapWidget->setStyleSheet("");
        }

        ui.showRealTimeButton->setEnabled(false);
        ui.toggleRecordingButton->setEnabled(false);
        ui.showRealTimeButton->setText("Show Real-Time Location");
        showingRealTimeLocation = false;
        QString jsCommand = "hideMarker();";
        webView->page()->runJavaScript(jsCommand);
        gnssData.clear();
        ui.infoTextBox->clear();
        ui.infoTextBox->append("Serial port has been disconnected.");
        return;
    }

    QString fileName = ui.fileNameLineEdit->text();
    if (fileName.isEmpty() || !fileName.endsWith(".csv", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, "Input Error", "Please enter a valid CSV file name.");
        return;
    }

    fileLocation = ui.fileLocationLineEdit->text();
    if (fileLocation.isEmpty()) {
        fileLocation = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }

    QDir directory(fileLocation);
    if (!directory.exists()) {
        QMessageBox::warning(this, "Directory Error", "The specified directory does not exist. Please select a valid directory by clicking 'Browse Directory'.");
        return;
    }

    QString fullPath = QDir(fileLocation).filePath(fileName);
    ui.fileLocationLineEdit->setText(fileLocation);
    fileLocation = fullPath;

    QFileInfo fileInfo(fileLocation);
    if (fileInfo.exists()) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "File Exists", "The CSV file already exists. Do you want to overwrite it?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QFile::remove(fileLocation);
            // Create a new file after removing the old one
            QFile newFile(fileLocation);
            if (!newFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QMessageBox::warning(this, "File Error", "Unable to overwrite the specified CSV file.");
                return;
            }
            QTextStream out(&newFile);
            out << "Date,Time,Latitude,N/S Indicator,Longitude,E/W Indicator,Speed (km/h),Number of Satellites in View,Number of Active Satellites,Active Satellite IDs,Average SNR\n";
            newFile.close();
        }
        else {
            return;
        }
    }
    else {
        QFile file(fileLocation);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "File Error", "Unable to create the specified CSV file.");
            return;
        }
        QTextStream out(&file);
        // Write the new CSV header
        out << "Date,Time,Latitude,N/S Indicator,Longitude,E/W Indicator,Speed (km/h),Number of Satellites in View,Number of Active Satellites,Active Satellite IDs,Average SNR\n";
        file.close();
    }

    QString baudRateText = ui.baudRateLineEdit->text();
    bool ok;
    baudRate = baudRateText.toInt(&ok);
    if (!ok || baudRate <= 0) {
        QMessageBox::warning(this, "Input Error", "Please enter a valid Baud Rate.");
        return;
    }

    comPort = ui.comPortLineEdit->text();
    if (comPort.isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please enter a valid COM Port.");
        return;
    }

    updateFrequency = ui.updateFrequencyComboBox->currentText().toInt(&ok);
    if (!ok || updateFrequency <= 0) {
        QMessageBox::warning(this, "Input Error", "Please select a valid update frequency.");
        return;
    }

    serial->setPortName(comPort);
    serial->setBaudRate(baudRate);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);

    if (serial->open(QIODevice::ReadOnly)) {
        ui.connectButton->setText("Disconnect");

        // Disable settings controls when connected
        ui.baudRateLineEdit->setEnabled(false);
        ui.comPortLineEdit->setEnabled(false);
        ui.fileLocationLineEdit->setEnabled(false);
        ui.fileNameLineEdit->setEnabled(false); 
        ui.browseButton->setEnabled(false);  
        ui.updateFrequencyComboBox->setEnabled(false);

        ui.showRealTimeButton->setEnabled(true);
        ui.infoTextBox->clear();
        ui.infoTextBox->append("Serial port has been connected.");
        ui.infoTextBox->append("Baud Rate: " + QString::number(baudRate));
        ui.infoTextBox->append("COM Port: " + comPort);
        ui.infoTextBox->append("File Location: " + fileLocation);
        ui.infoTextBox->append("Update Frequency: " + QString::number(updateFrequency));
    }
    else {
        QMessageBox::warning(this, "Input Error", "Sorry, unable to connect with serial port.");
    }
}



void GNSS_desktop_application::onToggleRecordingButtonClicked()
{
    if (!recordingData) {
        recordingData = true;
        ui.toggleRecordingButton->setText("Stop Recording");

        csvFile.setFileName(fileLocation);
        if (!csvFile.open(QIODevice::Append | QIODevice::Text)) {
            QMessageBox::warning(this, "File Error", "Unable to open the CSV file for recording.");
            recordingData = false;
            ui.toggleRecordingButton->setText("Start Recording");
            return;
        }
        ui.mapWidget->setStyleSheet("border: 3px solid red;");
    }
    else {
        recordingData = false;
        ui.toggleRecordingButton->setText("Start Recording");

        if (csvFile.isOpen())
            csvFile.close();

        ui.mapWidget->setStyleSheet("");
    }
}

void GNSS_desktop_application::changeLocation(double lat, double lng, double SNR)
{
    if (showingRealTimeLocation) {
        QString jsCommand = QString("updateMarkerPosition(%1, %2, '%3', %4);")
            .arg(QString::number(lat, 'g', 17))  // Full precision for latitude
            .arg(QString::number(lng, 'g', 17))  // Full precision for longitude
            .arg(CSVUtils::createMarkerTitle(gnssData))
            .arg(SNR, 0, 'f', 2);
        webView->page()->runJavaScript(jsCommand);
    }
}


void GNSS_desktop_application::onShowRealTimeButtonClicked()
{
    if (!webView) return;

    if (showingRealTimeLocation) {
        QString jsCommand = "hideRealTimeMarker();";
        webView->page()->runJavaScript(jsCommand);
        ui.showRealTimeButton->setText("Show Real-Time Location");
        showingRealTimeLocation = false;
        ui.toggleRecordingButton->setEnabled(false);
    }
    else {
        if (gnssData.isValid()) {
            QString jsCommand = "showRealTimeMarker();";
            webView->page()->runJavaScript(jsCommand);
            ui.showRealTimeButton->setText("Hide Real-Time Location");
            showingRealTimeLocation = true;
            ui.toggleRecordingButton->setEnabled(true);
        }
		else {
			QMessageBox::warning(this, "Data Error", "No valid GNSS data available for real-time location.");
		}
    }
}
void GNSS_desktop_application::onLoadHistoricalDataButtonClicked() {
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open CSV File"), "", tr("CSV Files (*.csv)"));

    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "File Error", "Unable to open the selected CSV file.");
        return;
    }

    // Disconnect Serial Port if connected
    if (serial->isOpen()) {
        serial->close();
        ui.connectButton->setText("Connect");
        ui.baudRateLineEdit->setEnabled(true);
        ui.comPortLineEdit->setEnabled(true);
        ui.fileLocationLineEdit->setEnabled(true);
        ui.fileNameLineEdit->setEnabled(true);
        ui.browseButton->setEnabled(true);
        ui.updateFrequencyComboBox->setEnabled(true);
        ui.showRealTimeButton->setEnabled(false);
        ui.toggleRecordingButton->setEnabled(false);
        ui.showRealTimeButton->setText("Show Real-Time Location");
        showingRealTimeLocation = false;
        gnssData.clear();
        ui.infoTextBox->append("Serial port has been disconnected.");
    }

    QTextStream in(&file);
    QStringList headers = in.readLine().split(',');

    if (headers.size() < 11 || headers[0] != "Date" || headers[1] != "Time" || headers[2] != "Latitude" ||
        headers[3] != "N/S Indicator" || headers[4] != "Longitude" || headers[5] != "E/W Indicator" ||
        headers[6] != "Speed (km/h)" || headers[7] != "Number of Satellites in View" ||
        headers[8] != "Number of Active Satellites" || headers[9] != "Active Satellite IDs" || headers[10] != "Average SNR") {
        QMessageBox::warning(this, "File Format Error", "The selected file does not match the expected CSV format.");
        return;
    }

    QStringList lines = in.readAll().split('\n');
    file.close();

    if (lines.isEmpty()) {
        QMessageBox::warning(this, "File Error", "The selected file is empty.");
        return;
    }

    QString jsCommand = "";
    int dataPointCount = 0;
    QDateTime firstTimestamp, lastTimestamp;

    for (const QString& line : lines) {
        if (line.trimmed().isEmpty()) continue;
        QStringList fields = line.split(',');

        // Check if we have the correct number of fields (11 in this case)
        if (fields.size() != 11) {
            qDebug() << "Skipping line due to incorrect number of fields: " << line;
            continue;
        }

        // Parse basic GNSS data
        double lat = fields[2].toDouble();
        double lng = fields[4].toDouble();
        double speedKmh = fields[6].toDouble();   // Speed in km/h
        int satellitesInView = fields[7].toInt();
        int activeSatellitesCount = fields[8].toInt();
        QString activeSatellitesIDs = fields[9].replace(";", ","); 
        double SNR = fields[10].split(" ").value(0).toDouble();
        QDateTime timestamp = QDateTime::fromString(fields[0] + " " + fields[1], "yyyy-MM-dd hh:mm:ss");
        if (!timestamp.isValid()) {
            qDebug() << "Skipping line due to invalid timestamp: " << line;
            continue;  // Skip this line if the timestamp is invalid
        }
        if (dataPointCount == 0) {
            firstTimestamp = timestamp;
        }
        lastTimestamp = timestamp;

        dataPointCount++;

        // Create a GNSSData object to populate the title
        GNSSData historicalData;
        historicalData.date = fields[0];
        historicalData.utc_time = fields[1];
        historicalData.latitude_dd = lat;
        historicalData.longitude_dd = lng;
        historicalData.ns_indicator = fields[3];
        historicalData.ew_indicator = fields[5];
        historicalData.speed_over_ground = QString::number(speedKmh);  // Store speed as string
        historicalData.num_satellites = satellitesInView;
        historicalData.total_active_satellites = activeSatellitesCount;

        // Parse and store the active satellite info
        QStringList activeSatIDs = activeSatellitesIDs.split(", ");  // Split using semicolons
        for (const QString& satID : activeSatIDs) {
            SatelliteInfo satInfo;
            satInfo.is_active = true;  // Mark this satellite as active
            satInfo.talker_id = satID.left(2);  // Extract the talker ID (e.g., "GP")
            satInfo.sat_id = satID.mid(2).toInt();  // Extract the satellite ID (e.g., "2")
            satInfo.snr_1 = SNR;  
            satInfo.snr_2 = -1;  // Default second signal is invalid

            historicalData.satellites.push_back(satInfo);
        }

        // Generate the marker title
        QString title = CSVUtils::createMarkerTitle(historicalData);
        title.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n");

        jsCommand += QString("addHistoricalMarker(%1, %2, \"%3\", %4);")
            .arg(QString::number(lat, 'g', 17))  // Full precision for latitude
            .arg(QString::number(lng, 'g', 17))  // Full precision for longitude
            .arg(title)
            .arg(SNR);
    }

    // Execute JavaScript to add all historical markers on the map
    if (!jsCommand.isEmpty()) {
        qDebug() << "JavaScript Command: " << jsCommand;
        webView->page()->runJavaScript(jsCommand);
    }

    QMessageBox::information(this, "Data Loaded", "Historical data has been loaded successfully.");
    ui.infoTextBox->clear();
    ui.infoTextBox->append(QString("Number of data points loaded: %1").arg(dataPointCount));

    if (firstTimestamp.isValid() && lastTimestamp.isValid()) {
        qint64 durationSeconds = firstTimestamp.secsTo(lastTimestamp);
        int hours = durationSeconds / 3600;
        int minutes = (durationSeconds % 3600) / 60;
        int seconds = durationSeconds % 60;
        ui.infoTextBox->append(QString("Recording duration: %1 hours, %2 minutes, %3 seconds")
            .arg(hours).arg(minutes).arg(seconds));
    }
    else {
        ui.infoTextBox->append("Unable to calculate recording duration.");
    }

    QString jsShowHistoricalCommand = "showHistoricalMarkers();";
    webView->page()->runJavaScript(jsShowHistoricalCommand);
}


void GNSS_desktop_application::onClearMapButtonClicked()
{
    QString jsClearCommand = "clearHistoricalMarkers();";
    webView->page()->runJavaScript(jsClearCommand);

    QString jsHideCommand = "hideRealTimeMarker();";
    webView->page()->runJavaScript(jsHideCommand);

    if (showingRealTimeLocation) {
        showingRealTimeLocation = false;
        ui.showRealTimeButton->setText("Show Real-Time Location");
        ui.toggleRecordingButton->setEnabled(false);
    }

    ui.infoTextBox->clear();
    ui.infoTextBox->append("Map has been cleared.");
}

void GNSS_desktop_application::closeEvent(QCloseEvent* event)
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Exit", "Are you sure you want to close the application?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (serial->isOpen()) {
            serial->close();
        }
        if (csvFile.isOpen()) {
            csvFile.close();
        }

        event->accept();
    }
    else {
        event->ignore();
    }
}
