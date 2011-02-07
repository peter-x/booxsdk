
#include "onyx/sys/service.h"
#include "onyx/sys/wpa_connection_manager.h"



static WifiProfile dummy;

WpaConnectionManager::WpaConnectionManager()
#ifndef _WINDOWS
: connection_(QDBusConnection::systemBus())
#else
: connection_(QDBusConnection::sessionBus())
#endif
, scan_count_(0)
, internal_state_(WpaConnection::STATE_UNKNOWN)
, auto_connect_(true)
, auto_reconnect_(true)
, wifi_enabled_(true)
{
    setupConnections();
    scan_timer_.setInterval(1500);
}

WpaConnectionManager::~WpaConnectionManager()
{
}

// Reset state machine and start the following actions
// - scan
// - connect to certain if ap is connected before
// - automatically fill password if necessary
// - broadcast signals when state changed
bool WpaConnectionManager::start()
{
    triggerScan();
    return true;
}

bool WpaConnectionManager::stop()
{
    enableSdio(false);
    return true;
}

void WpaConnectionManager::onSdioChanged(bool on)
{
    if (on)
    {
        start();
    }
    else
    {
        stop();
    }
}

void WpaConnectionManager::onScanTimeout()
{
    // If we can not connect to wpa supplicant before, we need to
    // scan now.
    scan();
}

/// Called when wpa supplicant get some scan results.
void WpaConnectionManager::onScanReturned(WifiProfiles & list)
{
    // When connecting, we may igonre the list.
    // TODO, we can also merge them into together.
    if (isConnecting())
    {
        qDebug("Don't trigger scan as it's in connecting state.");
        return;
    }

    // If there is no scan result, we retry.
    if (list.size() <= 0)
    {
        scan();
        return;
    }

    setState(WpaConnection::STATE_SCANNED);
    emit connectionChanged(dummy, WpaConnection::STATE_SCANNED);

    scan_results_ = list;
    scan_timer_.stop();
    connectToBestAP();
}

void WpaConnectionManager::scanResults(WifiProfiles &ret)
{
    ret = scan_results_;
}

WifiProfile WpaConnectionManager::connectingAP()
{
    return proxy().connectingAP();
}

void WpaConnectionManager::onNeedPassword(WifiProfile profile)
{
    // check if it is correct
    if (checkAuthentication(profile))
    {
        proxy().connectTo(profile);
        return;
    }

    emit passwordRequired(profile);
}

void WpaConnectionManager::onConnectionChanged(WifiProfile profile,
                                               WpaConnection::ConnectionState state)
{
    qDebug("state changed %d", state);
    switch (state)
    {
    case WpaConnection::STATE_SCANNING:
        {
            emit connectionChanged(dummy, WpaConnection::STATE_SCANNING);
        }
        break;

    case WpaConnection::STATE_CONNECTED:
        {
            saveAp(profile);
            emit connectionChanged(profile, WpaConnection::STATE_CONNECTED);
        }
        break;

    case WpaConnection::STATE_COMPLETE:
        {
            stopAllTimers();
            emit connectionChanged(profile, WpaConnection::STATE_COMPLETE);
        }
        break;
    case WpaConnection::STATE_ACQUIRING_ADDRESS_ERROR:
        {
            resetProfile(profile);
            emit connectionChanged(profile, WpaConnection::STATE_ACQUIRING_ADDRESS_ERROR);
        }
        break;

    case WpaConnection::STATE_AUTHENTICATION_FAILED:
        {
            // Could be ignored now.
            if (!checkAuthentication(profile))
            {
                onNeedPassword(profile);
            }
        }
        break;
    default:
        break;
    }
}

void WpaConnectionManager::onConnectionTimeout()
{
    qWarning("Connection timeout, maybe need to reconnect.");
}

void WpaConnectionManager::onComplete()
{
    setState(WpaConnection::STATE_COMPLETE);
}

bool WpaConnectionManager::checkWifiDevice()
{
    // Always enable sdio.
    enableSdio(true);

    // Check state again.
    wifi_enabled_ = sdioState();
    if (!wifi_enabled_)
    {
        setState(WpaConnection::STATE_DISABLED);
        return false;
    }
    return true;
}

bool WpaConnectionManager::checkWpaSupplicant()
{
    // Check wpa supplicant is running or not.
    if (!isWpaSupplicantRunning())
    {
        if (!startWpaSupplicant(""))
        {
            qWarning("Seems we can not start wpa supplicant.");
            emit wpaStateChanged(false);
            return false;
        }
    }

    if (proxy().openCtrlConnection() >= 0)
    {
        emit wpaStateChanged(true);
        return true;
    }
    emit wpaStateChanged(false);
    return false;
}

void WpaConnectionManager::setupConnections()
{
    QObject::connect(&scan_timer_, SIGNAL(timeout()), this, SLOT(onScanTimeout()));

    if (!connection_.connect(service, object, iface,
                             "sdioChangedSignal",
                             this,
                             SLOT(onSdioChanged(bool))))
    {
        qDebug("\nCan not connect the sdioChangedSignal!\n");
    }

    QObject::connect(&proxy(), SIGNAL(scanResultsReady(WifiProfiles &)),
            this, SLOT(onScanReturned(WifiProfiles &)));
    QObject::connect(&proxy(), SIGNAL(stateChanged(WifiProfile,WpaConnection::ConnectionState)),
        this, SLOT(onConnectionChanged(WifiProfile, WpaConnection::ConnectionState)));
    QObject::connect(&proxy(), SIGNAL(needPassword(WifiProfile )),
            this, SLOT(onNeedPassword(WifiProfile )));
}

void WpaConnectionManager::triggerScan()
{
    setConnecting(false);
    resetScanRetry();
    scan();
}

void WpaConnectionManager::scan()
{
    // Always check wifi device.
    if (!checkWifiDevice())
    {
        return;
    }

    increaseScanRetry();
    bool wpa_ok = checkWpaSupplicant();
    if (wpa_ok && canScanRetry())
    {
        setState(WpaConnection::STATE_SCANNING);
        proxy().scan();
    }
    else
    {
        // Wait a little bit to rescan.
        scan_timer_.stop();
        if (canScanRetry())
        {
            scan_timer_.start();
        }
        else if (!wpa_ok)
        {
            // Wifi device is detected, but wpa_supplicant can not be launched
            // Hardware issue, but user can try to turn off and turn on the
            // wifi switcher again.
            setState(WpaConnection::STATE_HARDWARE_ERROR);
        }
    }
}

void WpaConnectionManager::resetProfile(WifiProfile & profile)
{
    // TODO, not sure.
    setPassword(profile, QString());
    profile.setCount(-1);
    saveAp(profile);
}

void WpaConnectionManager::setPassword(WifiProfile & profile, const QString & password)
{
    if (profile.isWpa() || profile.isWpa2())
    {
        profile.setPsk(password);
    }
    else if (profile.isWep())
    {
        profile.setWepKey1(password);
    }
}

bool WpaConnectionManager::checkAuthentication(WifiProfile & profile)
{
    // Retry to use profiles that have been used.
    sys::SystemConfig conf;
    WifiProfiles & all = records(conf);
    conf.close();

    foreach(WifiProfile record, all)
    {
        // Use bssid instead of ssid.
        if (profile.bssid() == record.bssid())
        {
            if (profile.retry() <= 2)
            {
                if (syncAuthentication(record, profile))
                {
                    profile.setCount(record.count());
                    profile.setRetry(profile.retry() + 1);
                    return (profile.retry() <= 2);
                }
            }
        }
    }
    return false;
}

bool WpaConnectionManager::syncAuthentication(WifiProfile & source,
                                              WifiProfile & target)
{
    if (source.isWep() && target.isWep())
    {
        target.setWepKey1(source.wepKey1());
        target.setAuthAlg(source.authAlg());
        return true;
    }

    if ((source.isWpa() && target.isWpa()) ||
        (source.isWpa2() && target.isWpa2()))
    {
        if (source.psk().isEmpty())
        {
            return false;
        }
        target.setPsk(source.psk());
        return true;
    }
    return false;
}

void WpaConnectionManager::saveAp(WifiProfile & profile)
{
    sys::SystemConfig conf;
    WifiProfiles & all = records(conf);

    profile.increaseCount();
    for(int i = 0; i < records_->size(); ++i)
    {
        if (all[i].bssid() == profile.bssid())
        {
            profile.resetRetry();
            all.replace(i, profile);
            conf.saveWifiProfiles(all);
            return;
        }
    }

    all.push_front(profile);
    conf.saveWifiProfiles(all);
}

bool WpaConnectionManager::connectToBestAP()
{
    if (!allowAutoConnect())
    {
        qDebug("Auto connect is disabled.");
        return false;
    }

    if (scan_results_.size() <= 0)
    {
        qDebug("Don't connect as there is no access points available.");
        return false;
    }

    // Steps:
    // - Load all records and sort them according to count.
    // - Sort scan results according to the signal strength.
    // - Check if it's connected before, if ok, connect.
    sortByQuality(scan_results_);

    sys::SystemConfig conf;
    WifiProfiles & all = records(conf);
    conf.close();
    sortByCount(all);

    foreach(WifiProfile record, all)
    {
        for(int i = 0; i < scan_results_.size(); ++i)
        {
            if (scan_results_[i].bssid() == record.bssid())
            {
                syncAuthentication(record, scan_results_[i]);
                scan_results_[i].setCount(record.count());
            }
        }
    }

    sortByCount(scan_results_);

    setConnecting(true);
    emit connectionChanged(scan_results_.front(), WpaConnection::STATE_CONNECTING);
    proxy().connectTo(scan_results_.front());
    return true;
}

bool WpaConnectionManager::isConnecting()
{
    return (state() == WpaConnection::STATE_CONNECTING);
}

void WpaConnectionManager::setConnecting(bool c)
{
    if (c)
    {
        setState(WpaConnection::STATE_CONNECTING);
    }
    else
    {
        setState(WpaConnection::STATE_UNKNOWN);
    }
}

void WpaConnectionManager::stopAllTimers()
{
    scan_timer_.stop();
}

void WpaConnectionManager::setState(WpaConnection::ConnectionState s)
{
    internal_state_ = s;
}

WifiProfiles & WpaConnectionManager::records(sys::SystemConfig& conf)
{
    if (!records_)
    {
        records_.reset(new WifiProfiles);
        conf.loadWifiProfiles(*records_);
    }
    return *records_;
}

WpaConnection & WpaConnectionManager::proxy()
{
    if (!proxy_)
    {
        proxy_.reset(new WpaConnection(DEFAULT_INTERFACE));
    }
    return *proxy_;
}


bool WpaConnectionManager::enableSdio(bool enable) const
{
#ifdef WIN32
    return true;
#endif
    QDBusMessage message = QDBusMessage::createMethodCall(
        service,            // destination
        object,             // path
        iface,              // interface
        "enableSdio"      // method.
    );

    message << enable;
    QDBusMessage reply = connection_.call(message);

    if (reply.type() == QDBusMessage::ReplyMessage)
    {
        return checkAndReturnBool(reply.arguments());
    }
    else if (reply.type() == QDBusMessage::ErrorMessage)
    {
        qWarning("%s", qPrintable(reply.errorMessage()));
    }
    return false;
}

bool WpaConnectionManager::sdioState() const
{
#ifdef WIN32
    return true;
#endif

    QDBusMessage message = QDBusMessage::createMethodCall(
        service,            // destination
        object,             // path
        iface,              // interface
        "sdioState"      // method.
    );

    QDBusMessage reply = connection_.call(message);

    if (reply.type() == QDBusMessage::ReplyMessage)
    {
        return checkAndReturnBool(reply.arguments());
    }
    else if (reply.type() == QDBusMessage::ErrorMessage)
    {
        qWarning("%s", qPrintable(reply.errorMessage()));
    }
    return false;
}

bool WpaConnectionManager::enableSdio(bool enable)
{
#ifdef WIN32
    return true;
#endif
    QDBusMessage message = QDBusMessage::createMethodCall(
        service,            // destination
        object,             // path
        iface,              // interface
        "enableSdio"      // method.
    );

    message << enable;
    QDBusMessage reply = connection_.call(message);

    if (reply.type() == QDBusMessage::ReplyMessage)
    {
        return checkAndReturnBool(reply.arguments());
    }
    else if (reply.type() == QDBusMessage::ErrorMessage)
    {
        qWarning("%s", qPrintable(reply.errorMessage()));
    }
    return false;
}

bool WpaConnectionManager::isWpaSupplicantRunning()
{
#ifdef WIN32
    return true;
#endif

    QDBusMessage message = QDBusMessage::createMethodCall(
        service,            // destination
        object,             // path
        iface,              // interface
        "isWpaSupplicantRunning"      // method.
    );

    QDBusMessage reply = connection_.call(message);
    if (reply.type() == QDBusMessage::ReplyMessage)
    {
        return checkAndReturnBool(reply.arguments());
    }
    else if (reply.type() == QDBusMessage::ErrorMessage)
    {
        qWarning("%s", qPrintable(reply.errorMessage()));
    }
    return false;
}

bool WpaConnectionManager::startWpaSupplicant(const QString & conf_file_path)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        service,            // destination
        object,             // path
        iface,              // interface
        "startWpaSupplicant"      // method.
    );
    message << conf_file_path;

    QDBusMessage reply = connection_.call(message);
    if (reply.type() == QDBusMessage::ReplyMessage)
    {
        return checkAndReturnBool(reply.arguments());
    }
    else if (reply.type() == QDBusMessage::ErrorMessage)
    {
        qWarning("%s", qPrintable(reply.errorMessage()));
    }
    return false;
}

bool WpaConnectionManager::stopWpaSupplicant()
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        service,            // destination
        object,             // path
        iface,              // interface
        "stopWpaSupplicant"      // method.
    );

    QDBusMessage reply = connection_.call(message);
    if (reply.type() == QDBusMessage::ReplyMessage)
    {
        return checkAndReturnBool(reply.arguments());;
    }
    else if (reply.type() == QDBusMessage::ErrorMessage)
    {
        qWarning("%s", qPrintable(reply.errorMessage()));
    }
    return false;
}

