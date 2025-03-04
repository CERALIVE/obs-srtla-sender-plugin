/**
 * SRTLA Sender Plugin for OBS Studio
 * Main plugin implementation
 * 
 * This plugin enables OBS to connect to an SRTLA server for streaming.
 * It provides bidirectional synchronization between OBS stream settings and the SRTLA sender.
 * 
 * Author: Andres Cera
 * License: GPL-3.0
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QDialog>
#include <QCoreApplication>
#include <QMetaObject>
#include <qmessagebox.h>
#include <string>
#include <chrono>
#include "srtla-relay.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-srtla-sender", "en-US")

// Global SRTLA sender instance
SrtlaRelay* g_srtlaRelay = nullptr;

// Function declarations
static void add_srtla_menu_items();
static void open_srtla_settings();
static void start_srtla_sender();
static void stop_srtla_sender();

// Dialog for SRTLA settings
class SRTLASettingsDialog : public QDialog {
public:
    SRTLASettingsDialog(QWidget *parent) : QDialog(parent) {
        setWindowTitle("SRTLA Settings");
        setMinimumWidth(400);

        // Create form inputs
        serverEdit = new QLineEdit(this);
        if (g_srtlaRelay) {
            serverEdit->setText(QString::fromStdString(g_srtlaRelay->getServer()));
        }

        portEdit = new QSpinBox(this);
        portEdit->setRange(1, 65535);
        portEdit->setValue(g_srtlaRelay ? g_srtlaRelay->getPort() : 3000);

        streamIdEdit = new QLineEdit(this);
        if (g_srtlaRelay) {
            streamIdEdit->setText(QString::fromStdString(g_srtlaRelay->getStreamId()));
        }
        
        // Create latency slider with label
        latencySlider = new QSlider(Qt::Horizontal, this);
        latencySlider->setRange(1000, 8000);  // 1000ms to 8000ms
        latencySlider->setSingleStep(100);
        latencySlider->setPageStep(500);
        latencySlider->setValue(g_srtlaRelay ? g_srtlaRelay->getLatency() : 2000);  // Default: 2000ms
        
        latencyLabel = new QLabel(QString("Latency: %1 ms").arg(latencySlider->value()), this);
        connect(latencySlider, &QSlider::valueChanged, [this](int value) {
            latencyLabel->setText(QString("Latency: %1 ms").arg(value));
        });
        
        // Create auto-start checkbox
        autoStartCheckbox = new QCheckBox("Auto-start SRTLA when streaming starts", this);
        autoStartCheckbox->setChecked(g_srtlaRelay ? g_srtlaRelay->isAutoStartEnabled() : false);
        
        // Create fixed port checkbox and input
        useFixedPortCheckbox = new QCheckBox("Use fixed local port:", this);
        useFixedPortCheckbox->setChecked(g_srtlaRelay ? g_srtlaRelay->isFixedPortEnabled() : true);
        
        localPortEdit = new QSpinBox(this);
        localPortEdit->setRange(1024, 65535);
        localPortEdit->setValue(g_srtlaRelay ? g_srtlaRelay->getLocalPort() : 9000);
        localPortEdit->setEnabled(useFixedPortCheckbox->isChecked());
        
        // Connect checkbox to enable/disable port input
        connect(useFixedPortCheckbox, &QCheckBox::toggled, localPortEdit, &QSpinBox::setEnabled);
        
        // Create bidirectional sync checkbox and info label
        bidirectionalSyncCheckbox = new QCheckBox("Bidirectional sync with OBS Stream Settings", this);
        bidirectionalSyncCheckbox->setChecked(g_srtlaRelay ? g_srtlaRelay->isBidirectionalSyncEnabled() : true);
        
        QLabel *syncInfoLabel = new QLabel("When enabled, SRTLA settings will sync with OBS Stream Server URL, and vice versa.\n"
                                         "This ensures consistency between SRTLA relay and OBS streaming settings.", this);
        syncInfoLabel->setWordWrap(true);
        
        // Create a layout for the fixed port checkbox and spinbox
        QHBoxLayout *portLayout = new QHBoxLayout;
        portLayout->addWidget(useFixedPortCheckbox);
        portLayout->addWidget(localPortEdit);
        portLayout->addStretch();
        
        // Create a layout for the fixed port info label
        QLabel *portInfoLabel = new QLabel("When bidirectional sync is enabled, fixed port is always used.", this);
        portInfoLabel->setWordWrap(true);
        
        // Connect bidirectional sync checkbox to fixed port checkbox
        connect(bidirectionalSyncCheckbox, &QCheckBox::toggled, [this](bool checked) {
            if (checked) {
                useFixedPortCheckbox->setChecked(true);
                useFixedPortCheckbox->setEnabled(false);
            } else {
                useFixedPortCheckbox->setEnabled(true);
            }
        });
        
        // Set initial state based on bidirectional sync setting
        if (bidirectionalSyncCheckbox->isChecked()) {
            useFixedPortCheckbox->setChecked(true);
            useFixedPortCheckbox->setEnabled(false);
        }
        
        // Create layout for sync button
        QHBoxLayout *syncButtonLayout = new QHBoxLayout;
        syncButtonLayout->addWidget(bidirectionalSyncCheckbox);
        syncButtonLayout->addStretch();
        
        // Create buttons
        QPushButton *saveButton = new QPushButton("Save", this);
        QPushButton *cancelButton = new QPushButton("Cancel", this);
        
        // Connect buttons
        connect(saveButton, &QPushButton::clicked, this, &SRTLASettingsDialog::saveSettings);
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        
        // Create button layout
        QHBoxLayout *buttonLayout = new QHBoxLayout;
        buttonLayout->addStretch();
        buttonLayout->addWidget(saveButton);
        buttonLayout->addWidget(cancelButton);
        
        // Create form layout
        QFormLayout *formLayout = new QFormLayout;
        formLayout->addRow("SRTLA Server:", serverEdit);
        formLayout->addRow("SRTLA Port:", portEdit);
        formLayout->addRow("Stream ID (Optional):", streamIdEdit);
        formLayout->addRow("SRT Latency:", latencySlider);
        formLayout->addRow("", latencyLabel);
        formLayout->addRow("Local Port:", portLayout);
        
        // Main layout
        QVBoxLayout *mainLayout = new QVBoxLayout;
        mainLayout->addLayout(formLayout);
        mainLayout->addWidget(autoStartCheckbox);
        mainLayout->addLayout(syncButtonLayout);  // Add sync checkbox and button
        mainLayout->addWidget(syncInfoLabel);     // Add sync description
        mainLayout->addWidget(portInfoLabel);
        mainLayout->addLayout(buttonLayout);
        
        setLayout(mainLayout);
    }
    
    void saveSettings() {
        std::string server = serverEdit->text().toStdString();
        uint16_t port = portEdit->value();
        std::string streamId = streamIdEdit->text().toStdString();
        bool autoStart = autoStartCheckbox->isChecked();
        int latency = latencySlider->value();
        bool useFixedPort = useFixedPortCheckbox->isChecked();
        uint16_t localPort = localPortEdit->value();
        bool bidirectionalSync = bidirectionalSyncCheckbox->isChecked();
        
        if (!g_srtlaRelay)
            return;
        
        // Store old values to track changes
        bool syncWasEnabled = g_srtlaRelay->isBidirectionalSyncEnabled();
        uint16_t oldPort = g_srtlaRelay->getLocalPort();
        int oldLatency = g_srtlaRelay->getLatency();
        std::string oldStreamId = g_srtlaRelay->getStreamId();
        
        // Get current OBS URL BEFORE making any changes (for notification)
        std::string currentOBSUrl = g_srtlaRelay->getCurrentOBSStreamServerURL();
        
        // Update all settings
        g_srtlaRelay->setServer(server);
        g_srtlaRelay->setPort(port);
        g_srtlaRelay->setStreamId(streamId);
        g_srtlaRelay->setAutoStart(autoStart);
        g_srtlaRelay->setLatency(latency);
        g_srtlaRelay->setUseFixedPort(useFixedPort);
        g_srtlaRelay->setLocalPort(localPort);
        g_srtlaRelay->setBidirectionalSync(bidirectionalSync);
        
        // Always use fixed port when bidirectional sync is enabled
        if (bidirectionalSync) {
            g_srtlaRelay->setUseFixedPort(true);
        }
        
        // Save settings to file
        g_srtlaRelay->saveSettings();
        
        blog(LOG_INFO, "SRTLA settings updated: server=%s, port=%d, stream_id=%s, latency=%d, use_fixed_port=%d, local_port=%d, bidirectional_sync=%d", 
             server.c_str(), port, streamId.c_str(), latency, useFixedPort, localPort, bidirectionalSync);
        
        // Build new URL for notification (we already have the current URL from above)
        std::string newSRTUrl = g_srtlaRelay->buildSRTURL(localPort, latency, streamId);
        
        // Display notification that settings were saved with URL comparison
        QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
        if (main_window) {
            // Check if the URLs are different
            bool urlsAreDifferent = (currentOBSUrl != newSRTUrl);
            
            // Only create a detailed notification if URLs are different and sync is enabled
            if (bidirectionalSync && urlsAreDifferent) {
                QString message = "SRTLA settings saved successfully!\n\n";
                message += "OBS Stream URL:\n";
                message += QString("Old: %1\n").arg(QString::fromStdString(currentOBSUrl));
                message += QString("New: %1\n\n").arg(QString::fromStdString(newSRTUrl));
                message += "This URL will be applied to OBS Settings → Stream → Server.";
                
                QMessageBox::information(main_window, "SRTLA Relay", message);
            } 
            // Simple notification if no URL changes
            else if (!urlsAreDifferent && bidirectionalSync) {
                // Don't show notification if URLs are the same
            }
            // Simple notification if bidirectional sync is disabled
            else {
                QMessageBox::information(main_window, "SRTLA Relay", "SRTLA settings saved successfully!");
            }
        }
        
        // If bidirectional sync is enabled, immediately update the OBS service URL
        if (bidirectionalSync) {
            // We already built the URL above for the notification, reuse it
            
            blog(LOG_INFO, "Current OBS URL: %s", currentOBSUrl.c_str());
            blog(LOG_INFO, "New SRT URL with latency: %s", newSRTUrl.c_str());
            
            // Always update when settings are saved, to ensure latency is included
            blog(LOG_INFO, "Updating OBS service URL on settings save...");
            if (g_srtlaRelay->syncToOBSService()) {
                blog(LOG_INFO, "Successfully updated OBS service with new settings including latency");
                
                // Use the notification from syncToOBSService to avoid duplicates
            }
        }
        
        // If SRTLA is running and local port changed, restart it
        if (g_srtlaRelay->isRunning() && oldPort != localPort) {
            blog(LOG_INFO, "Restarting SRTLA with new port: %d", localPort);
            g_srtlaRelay->restartWithPort(localPort);
        }
        
        accept();
    }
    
private:
    QLineEdit *serverEdit;
    QSpinBox *portEdit;
    QLineEdit *streamIdEdit;
    QSlider *latencySlider;
    QLabel *latencyLabel;
    QCheckBox *autoStartCheckbox;
    QCheckBox *useFixedPortCheckbox;
    QSpinBox *localPortEdit;
    QCheckBox *bidirectionalSyncCheckbox;
};

// Register our service
static void setup_srt_service() {
    blog(LOG_INFO, "Setting up 'SRTLA Relay' service in OBS");
    
    // Register our service with OBS
    extern struct obs_service_info srtla_service;
    obs_register_service(&srtla_service);
    blog(LOG_INFO, "Registered SRTLA Relay service with ID: %s", srtla_service.id);
}

// Open SRTLA settings dialog
static void open_srtla_settings() {
    if (!g_srtlaRelay)
        return;

    QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
    SRTLASettingsDialog dialog(main_window);

    dialog.exec();
}

// Start SRTLA relay
static void start_srtla_sender() {
    blog(LOG_INFO, "Start SRTLA sender request received");

    if (!g_srtlaRelay) {
        blog(LOG_ERROR, "SRTLA sender instance is null!");
        return;
    }

    if (g_srtlaRelay->isRunning()) {
        blog(LOG_INFO, "SRTLA sender is already running");
        return;
    }

    // Show an error if server is not configured
    if (g_srtlaRelay->getServer().empty()) {
        blog(LOG_WARNING, "SRTLA server not configured");
        QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
        if (main_window) {
            QMessageBox::warning(main_window, "SRTLA Relay",
                                "Please configure your SRTLA server settings first.");
            open_srtla_settings();
        } else {
            blog(LOG_ERROR, "Cannot show settings dialog - main window is null");
        }
        return;
    }

    blog(LOG_INFO, "Starting SRTLA sender...");
    bool success = g_srtlaRelay->startSrtlaProcess();
    
    if (success) {
        blog(LOG_INFO, "SRTLA sender started successfully");
        
        // Show notification
        QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
        if (main_window) {
            QString message = "SRTLA sender started with:\n";
            message += QString("Server: %1:%2\n").arg(QString::fromStdString(g_srtlaRelay->getServer()))
                                               .arg(g_srtlaRelay->getPort());
            message += QString("Local Port: %1\n").arg(g_srtlaRelay->getLocalPort());
            if (!g_srtlaRelay->getStreamId().empty()) {
                message += QString("Stream ID: %1").arg(QString::fromStdString(g_srtlaRelay->getStreamId()));
            }
            
            QMessageBox::information(main_window, "SRTLA Sender", message);
        }
    } else {
        blog(LOG_ERROR, "Failed to start SRTLA sender");
        
        // Show error
        QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
        if (main_window) {
            QMessageBox::critical(main_window, "SRTLA Sender", 
                                 "Failed to start SRTLA sender. Check OBS log for details.");
        }
    }
}

// Stop SRTLA sender
static void stop_srtla_sender() {
    blog(LOG_INFO, "Stop SRTLA sender request received");

    if (!g_srtlaRelay) {
        blog(LOG_ERROR, "SRTLA sender instance is null!");
        return;
    }

    if (!g_srtlaRelay->isRunning()) {
        blog(LOG_INFO, "SRTLA sender is not running");
        return;
    }

    blog(LOG_INFO, "Stopping SRTLA sender...");
    g_srtlaRelay->stopSrtlaProcess();
    blog(LOG_INFO, "SRTLA sender stopped");
    
    // Show notification
    QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
    if (main_window) {
        QMessageBox::information(main_window, "SRTLA Sender", "SRTLA sender stopped");
    }
}

// Add menu items to OBS Tools menu
static QAction *startStopAction = nullptr;

static void update_menu_text() {
    if (!startStopAction)
        return;
        
    if (g_srtlaRelay && g_srtlaRelay->isRunning()) {
        startStopAction->setText("Stop SRTLA Sender");
    } else {
        startStopAction->setText("Start SRTLA Sender");
    }
}

static void toggle_srtla_sender() {
    if (!g_srtlaRelay)
        return;
        
    if (g_srtlaRelay->isRunning()) {
        stop_srtla_sender();
    } else {
        start_srtla_sender();
    }
    
    // Update menu text after toggling
    update_menu_text();
}

static void add_srtla_menu_items() {
    QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
    if (!main_window)
        return;

    // Create SRTLA submenu
    QMenu *srtlaMenu = new QMenu("SRTLA Sender", main_window);
    QAction *submenuAction = (QAction*)obs_frontend_add_tools_menu_qaction("SRTLA Sender");
    submenuAction->setMenu(srtlaMenu);
    
    // Add settings action to submenu
    QAction *settingsAction = srtlaMenu->addAction("Settings");
    QObject::connect(settingsAction, &QAction::triggered, [](bool checked) {
        UNUSED_PARAMETER(checked);
        open_srtla_settings();
    });
    
    // Create a single toggle action for Start/Stop in the submenu
    startStopAction = srtlaMenu->addAction("Start SRTLA Sender");
    QObject::connect(startStopAction, &QAction::triggered, [](bool checked) {
        UNUSED_PARAMETER(checked);
        toggle_srtla_sender();
    });
    
    // Set initial text
    update_menu_text();
}

// Front-end event callback
static void on_event(enum obs_frontend_event event, void *data) {
    UNUSED_PARAMETER(data);

    if (event == OBS_FRONTEND_EVENT_STREAMING_STARTING) {
        blog(LOG_INFO, "Streaming is starting");
        
        // Auto-start SRTLA if enabled
        if (g_srtlaRelay && g_srtlaRelay->isAutoStartEnabled() && !g_srtlaRelay->isRunning()) {
            blog(LOG_INFO, "Auto-starting SRTLA sender");
            
            // If server is not configured, show a warning
            if (g_srtlaRelay->getServer().empty()) {
                blog(LOG_WARNING, "SRTLA server not configured");
                QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
                QMetaObject::invokeMethod(QCoreApplication::instance(), [main_window]() {
                    if (main_window) {
                        QMessageBox::warning(main_window, "SRTLA Sender",
                                          "SRTLA server not configured. Please configure SRTLA server settings.");
                    }
                }, Qt::QueuedConnection);
                return;
            }
            
            if (g_srtlaRelay->startSrtlaProcess()) {
                blog(LOG_INFO, "SRTLA sender auto-started successfully");
                
                // Update menu text when auto-starting
                update_menu_text();
            } else {
                blog(LOG_ERROR, "Failed to auto-start SRTLA sender");
                
                // Show error using Qt's thread-safe way of showing message boxes
                QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
                QMetaObject::invokeMethod(QCoreApplication::instance(), [main_window]() {
                    if (main_window) {
                        QMessageBox::critical(main_window, "SRTLA Sender", 
                                           "Failed to auto-start SRTLA sender. Check OBS log for details.");
                    }
                }, Qt::QueuedConnection);
            }
        } else {
            blog(LOG_INFO, "SRTLA auto-start not enabled or already running");
        }
    }
    else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPING) {
        blog(LOG_INFO, "Streaming is stopping");
        
        // Auto-stop SRTLA if it was auto-started
        if (g_srtlaRelay && g_srtlaRelay->isRunning() && g_srtlaRelay->isAutoStartEnabled()) {
            blog(LOG_INFO, "Auto-stopping SRTLA sender");
            g_srtlaRelay->stopSrtlaProcess();
            blog(LOG_INFO, "SRTLA sender auto-stopped");
            
            // Update menu text when auto-stopping
            update_menu_text();
        }
    }
    // Monitor changes when the app has finished loading
    else if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        blog(LOG_INFO, "OBS frontend finished loading - checking for service changes");
        
        if (g_srtlaRelay && g_srtlaRelay->isBidirectionalSyncEnabled()) {
            blog(LOG_INFO, "Bidirectional sync is enabled, syncing settings");
            g_srtlaRelay->syncFromOBSService();
        }
    }
    // Monitor changes to the service
    else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
        blog(LOG_INFO, "Scene collection changed - checking for service changes");
        
        if (g_srtlaRelay && g_srtlaRelay->isBidirectionalSyncEnabled()) {
            blog(LOG_INFO, "Bidirectional sync is enabled, syncing settings");
            g_srtlaRelay->syncFromOBSService();
        }
    }
}

bool obs_module_load(void) {
    blog(LOG_INFO, "SRTLA Sender plugin loaded");

    // Create our plugin instance
    g_srtlaRelay = new SrtlaRelay();
    g_srtlaRelay->init();

    // Register our service
    setup_srt_service();

    // Add menu items
    add_srtla_menu_items();

    // Hook into frontend events for auto start/stop
    obs_frontend_add_event_callback(on_event, nullptr);
    
    // Set up a timer to periodically monitor service settings for changes
    QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
    if (main_window) {
        // First, before setting up timer, force sync settings from local storage to OBS
        // This ensures port values are persisted at startup
        if (g_srtlaRelay && g_srtlaRelay->isBidirectionalSyncEnabled()) {
            blog(LOG_INFO, "Forcing initial sync of saved settings to OBS at startup");
            g_srtlaRelay->syncToOBSService();
        }
        
        QTimer *serviceMonitorTimer = new QTimer(main_window);
        
        // Use a longer timer interval - only check during startup
        serviceMonitorTimer->setInterval(5000); // 5 seconds
        
        QObject::connect(serviceMonitorTimer, &QTimer::timeout, [=]() {
            // We only need timer for initial startup checks - don't do periodic checks per user request
            static bool startupSyncComplete = false;
            
            // If we've already done our startup checks, or sync not enabled, don't do anything
            if (startupSyncComplete || !g_srtlaRelay || !g_srtlaRelay->isBidirectionalSyncEnabled())
                return;
                
            // Get the current OBS stream server URL directly
            std::string currentOBSUrl = g_srtlaRelay->getCurrentOBSStreamServerURL();
            if (currentOBSUrl.empty())
                return;
            
            // Static variables to track changes between timer calls
            static std::string lastCheckedUrl = "";
            static int consecutiveValidChecks = 0;
            
            // Only do the first 3 checks, then stop checking
            if (consecutiveValidChecks >= 3) {
                startupSyncComplete = true;
                blog(LOG_INFO, "Startup synchronization complete, disabling periodic checks");
                return;
            }
            
            // Force sync during startup
            bool forceSync = true;
            
            // Log the startup check
            blog(LOG_INFO, "Performing startup sync check #%d: %s", 
                 consecutiveValidChecks + 1, currentOBSUrl.c_str());
            
            if (currentOBSUrl != lastCheckedUrl) {
                blog(LOG_INFO, "OBS stream server URL changed: %s", currentOBSUrl.c_str());
            } else if (forceSync) {
                blog(LOG_INFO, "Forcing URL sync for stability (check #%d): %s", 
                     consecutiveValidChecks + 1, currentOBSUrl.c_str());
            }
            
            // Check if this is an SRT URL (starts with srt://)
            if (currentOBSUrl.compare(0, 6, "srt://") == 0) {
                // Parse parameters from the URL
                uint16_t urlPort;
                int urlLatency;
                std::string urlStreamId;
                
                // Pass the current local port as input to avoid it being reset
                urlPort = g_srtlaRelay->getLocalPort();
                
                bool parseSuccess = g_srtlaRelay->extractSRTParamsFromURL(
                    currentOBSUrl, urlPort, urlLatency, urlStreamId);
                
                if (parseSuccess) {
                    blog(LOG_INFO, "Extracted from OBS URL - port: %d, latency: %d, streamId: %s", 
                         urlPort, urlLatency, urlStreamId.c_str());
                    
                    // Sync settings if they're different from SRTLA's current settings
                    bool needsSync = forceSync; // Force sync during first few checks
                    
                    // Check for port mismatch
                    if (urlPort > 0 && urlPort != g_srtlaRelay->getLocalPort()) {
                        blog(LOG_INFO, "Port changed in OBS URL: %d → %d", 
                             g_srtlaRelay->getLocalPort(), urlPort);
                        needsSync = true;
                    }
                    
                    // Check for latency mismatch - ALWAYS check latency
                    if (urlLatency != g_srtlaRelay->getLatency()) {
                        blog(LOG_INFO, "Latency changed in OBS URL: %d → %d", 
                             g_srtlaRelay->getLatency(), urlLatency);
                        needsSync = true;
                    }
                    
                    // Always check for missing latency in URL - this is crucial
                    if (currentOBSUrl.find("latency=") == std::string::npos) {
                        blog(LOG_INFO, "Latency parameter missing in OBS URL - must force sync");
                        needsSync = true;
                    }
                    
                    // Check for streamId mismatch
                    if (!urlStreamId.empty() && urlStreamId != g_srtlaRelay->getStreamId()) {
                        blog(LOG_INFO, "StreamID changed in OBS URL: %s → %s", 
                             g_srtlaRelay->getStreamId().c_str(), urlStreamId.c_str());
                        needsSync = true;
                    }
                    
                    // Sync in both directions if needed
                    if (needsSync) {
                        blog(LOG_INFO, "Changes detected in OBS URL, performing bidirectional sync");
                        
                        // First sync from OBS to SRTLA for any valid changes
                        if (g_srtlaRelay->syncFromOBSService()) {
                            blog(LOG_INFO, "Successfully synced OBS settings to SRTLA");
                            
                            // If SRTLA is running, restart with new port
                            if (g_srtlaRelay->isRunning()) {
                                uint16_t port = g_srtlaRelay->getLocalPort();
                                blog(LOG_INFO, "Restarting SRTLA with new port: %d", port);
                                g_srtlaRelay->restartWithPort(port);
                            }
                        }
                        
                        // Then force OBS to adopt our settings including the port
                        // This two-step process ensures full bidirectional sync
                        g_srtlaRelay->syncToOBSService();
                    } else {
                        blog(LOG_INFO, "No parameter changes detected in OBS URL");
                    }
                }
            } else {
                blog(LOG_INFO, "Non-SRT URL detected in OBS, converting to SRT format");
                
                // Sync to OBS to convert the URL to SRT format
                g_srtlaRelay->syncToOBSService();
            }
            
            // Update the last checked URL and increment valid check counter
            lastCheckedUrl = currentOBSUrl;
            consecutiveValidChecks++;
            
            // Log completion status
            if (consecutiveValidChecks >= 3) {
                startupSyncComplete = true;
                blog(LOG_INFO, "Startup synchronization complete, disabling periodic checks");
            }
        });
        
        // Start the timer with the interval we set above (5 seconds)
        serviceMonitorTimer->start();
    }
    
    blog(LOG_INFO, "Plugin initialization complete");
    return true;
}

// Called when the module is unloaded
void obs_module_unload(void) {
    blog(LOG_INFO, "SRTLA Sender plugin unloaded");

    if (g_srtlaRelay) {
        delete g_srtlaRelay;
        g_srtlaRelay = nullptr;
    }
}

// Instead of redefining obs_get_module, we'll expose our sender instance via a different method
SrtlaRelay* get_srtla_relay_instance() {
    return g_srtlaRelay;
}