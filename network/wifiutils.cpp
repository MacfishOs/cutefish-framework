/*
 * The SSID charset table and security checks are ported from NetworkManager's
 * src/libnm-core-impl/nm-utils.c:
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright (C) 2005 - 2017 Red Hat, Inc.
 */

#include "wifiutils.h"
#include "networkdbustypes.h"

#include <QLocale>
#include <QObject>
#include <QRandomGenerator>
#include <QStringDecoder>

#include <cctype>
#include <iconv.h>

using namespace NetworkDBus;

namespace {

struct LocaleEncodings {
    const char *locale;
    QList<const char *> encodings;
};

// NetworkManager's table in libnm-core-impl/nm-utils.c, by language and then country.
const LocaleEncodings localeEncodings[] = {
    {"zh_cn", {"GB18030", "EUC-CN", "GB2312"}},
    {"zh_sg", {"GB18030", "EUC-CN", "GB2312"}},
    {"zh_hk", {"BIG5", "BIG5-HKSCS", "EUC-TW"}},
    {"zh_mo", {"BIG5", "EUC-TW"}},
    {"zh_tw", {"BIG5", "EUC-TW"}},
    {"ar", {"ISO-8859-6", "WINDOWS-1256"}},
    {"be", {"KOI8-R", "WINDOWS-1251", "ISO-8859-5"}},
    {"bg", {"WINDOWS-1251", "KOI8-R", "ISO-8859-5"}},
    {"cs", {"ISO-8859-2", "WINDOWS-1250"}},
    {"el", {"ISO-8859-7", "WINDOWS-1253"}},
    {"et", {"ISO-8859-4", "WINDOWS-1257"}},
    {"he", {"ISO-8859-8", "WINDOWS-1255"}},
    {"hr", {"ISO-8859-2", "WINDOWS-1250"}},
    {"hu", {"ISO-8859-2", "WINDOWS-1250"}},
    {"iw", {"ISO-8859-8", "WINDOWS-1255"}},
    {"ja", {"EUC-JP", "SHIFT_JIS", "ISO-2022-JP"}},
    {"ko", {"EUC-KR", "ISO-2022-KR", "JOHAB"}},
    {"lt", {"ISO-8859-4", "WINDOWS-1257"}},
    {"lv", {"ISO-8859-4", "WINDOWS-1257"}},
    {"mk", {"KOI8-R", "WINDOWS-1251", "ISO-8859-5"}},
    {"pl", {"ISO-8859-2", "WINDOWS-1250"}},
    {"ro", {"ISO-8859-2", "WINDOWS-1250"}},
    {"ru", {"KOI8-R", "WINDOWS-1251", "ISO-8859-5"}},
    {"sh", {"ISO-8859-2", "WINDOWS-1250"}},
    {"sk", {"ISO-8859-2", "WINDOWS-1250"}},
    {"sl", {"ISO-8859-2", "WINDOWS-1250"}},
    {"sr", {"KOI8-R", "WINDOWS-1251", "ISO-8859-5"}},
    {"th", {"ISO-8859-11", "WINDOWS-874"}},
    {"tr", {"ISO-8859-9", "WINDOWS-1254"}},
    {"uk", {"KOI8-U", "KOI8-R", "WINDOWS-1251"}},
};

QList<const char *> encodingsForLocale()
{
    const QString name = QLocale::system().name().toLower();
    for (const auto &entry : localeEncodings) {
        if (name == QLatin1String(entry.locale))
            return entry.encodings;
    }
    const QString language = name.left(2);
    for (const auto &entry : localeEncodings) {
        if (language == QLatin1String(entry.locale))
            return entry.encodings;
    }
    return {};
}

bool decode(const QByteArray &bytes, const char *encoding, QString *text)
{
    iconv_t converter = iconv_open("UTF-8", encoding);
    if (converter == iconv_t(-1))
        return false;

    QByteArray output(bytes.size() * 4 + 4, Qt::Uninitialized);
    char *in = const_cast<char *>(bytes.constData());
    size_t inLeft = bytes.size();
    char *out = output.data();
    size_t outLeft = output.size();
    const size_t result = iconv(converter, &in, &inLeft, &out, &outLeft);
    iconv_close(converter);
    if (result == size_t(-1) || inLeft > 0)
        return false;

    output.truncate(output.size() - int(outLeft));
    *text = QString::fromUtf8(output);
    return true;
}

// A control character in a label would break the line it sits in.
QString printable(QString text)
{
    for (QChar &c : text) {
        if (c.category() == QChar::Other_Control)
            c = QChar::ReplacementCharacter;
    }
    return text;
}

bool supportsCiphers(uint caps, uint flags, bool staticWep)
{
    bool pair = staticWep;
    if (!staticWep) {
        pair = ((caps & CapWep40) && (flags & PairWep40)) || ((caps & CapWep104) && (flags & PairWep104))
                || ((caps & CapTkip) && (flags & PairTkip)) || ((caps & CapCcmp) && (flags & PairCcmp));
    }

    bool group = ((caps & CapWep40) && (flags & GroupWep40)) || ((caps & CapWep104) && (flags & GroupWep104));
    if (!staticWep)
        group = group || ((caps & CapTkip) && (flags & GroupTkip)) || ((caps & CapCcmp) && (flags & GroupCcmp));

    return pair && group;
}

bool pskCipher(uint caps, uint flags)
{
    return ((flags & PairTkip) && (caps & CapTkip)) || ((flags & PairCcmp) && (caps & CapCcmp));
}

// nm_utils_security_valid() for an access point that is in range.
bool valid(Enums::SecurityType type, uint caps, bool adhoc, uint apFlags, uint wpa, uint rsn)
{
    switch (type) {
    case Enums::NoneSecurity:
        return !(apFlags & ApPrivacy) && !wpa && !rsn;
    case Enums::Leap:
        if (adhoc)
            return false;
        [[fallthrough]];
    case Enums::StaticWep:
        if (!(apFlags & ApPrivacy))
            return false;
        if (wpa || rsn)
            return supportsCiphers(caps, wpa, true) || supportsCiphers(caps, rsn, true);
        return true;
    case Enums::DynamicWep:
        if (adhoc || rsn || !(apFlags & ApPrivacy))
            return false;
        // Some access points broadcast minimal WPA-enabled beacons.
        if (wpa)
            return (wpa & KeyMgmt8021x) && supportsCiphers(caps, wpa, false);
        return true;
    case Enums::WpaPsk:
        return !adhoc && (caps & CapWpa) && (wpa & KeyMgmtPsk) && pskCipher(caps, wpa);
    case Enums::Wpa2Psk:
        if (!(caps & CapRsn))
            return false;
        if (adhoc)
            return (caps & CapIbssRsn) && (rsn & PairCcmp) && (caps & CapCcmp);
        return (rsn & KeyMgmtPsk) && pskCipher(caps, rsn);
    case Enums::WpaEap:
        return !adhoc && (caps & CapWpa) && (wpa & KeyMgmt8021x) && supportsCiphers(caps, wpa, false);
    case Enums::Wpa2Eap:
        return !adhoc && (caps & CapRsn) && (rsn & KeyMgmt8021x) && supportsCiphers(caps, rsn, false);
    case Enums::SAE:
        return !adhoc && (caps & CapRsn) && (rsn & KeyMgmtSae) && (rsn & PairCcmp) && (caps & CapCcmp);
    case Enums::OWE:
        return !adhoc && (caps & CapRsn) && (rsn & (KeyMgmtOwe | KeyMgmtOweTm));
    case Enums::Wpa3SuiteB192:
        return !adhoc && (caps & CapRsn) && (rsn & KeyMgmtEapSuiteB192);
    default:
        return false;
    }
}

// What the access point advertises, for devices that report no capabilities.
Enums::SecurityType advertised(uint apFlags, uint wpa, uint rsn)
{
    if (rsn & KeyMgmtEapSuiteB192)
        return Enums::Wpa3SuiteB192;
    if (rsn & KeyMgmtSae)
        return Enums::SAE;
    if (rsn & KeyMgmt8021x)
        return Enums::Wpa2Eap;
    if (rsn & KeyMgmtPsk)
        return Enums::Wpa2Psk;
    if (wpa & KeyMgmt8021x)
        return Enums::WpaEap;
    if (wpa & KeyMgmtPsk)
        return Enums::WpaPsk;
    if (rsn & (KeyMgmtOwe | KeyMgmtOweTm))
        return Enums::OWE;
    if (apFlags & ApPrivacy)
        return Enums::StaticWep;
    return Enums::NoneSecurity;
}

} // namespace

namespace WifiUtils {

QString ssidLabel(const QByteArray &ssid)
{
    const QByteArray bytes = ssidKey(ssid);

    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = utf8(bytes);
    if (!utf8.hasError())
        return printable(text);

    for (const char *encoding : encodingsForLocale()) {
        QString decoded;
        if (decode(bytes, encoding, &decoded))
            return printable(decoded);
    }

    return printable(QString::fromLatin1(bytes));
}

bool ssidHidden(const QByteArray &ssid)
{
    if (ssid == " ")
        return true;
    for (const char c : ssid) {
        if (c != '\0')
            return false;
    }
    return true;
}

QByteArray ssidKey(const QByteArray &ssid)
{
    if (ssid.endsWith('\0'))
        return ssid.chopped(1);
    return ssid;
}

Enums::SecurityType security(uint deviceCapabilities, uint apFlags, uint wpaFlags, uint rsnFlags,
                             uint mode)
{
    // In NetworkManager-Qt's order: security level first, then how common each is.
    static const Enums::SecurityType order[] = {
        Enums::Wpa3SuiteB192, Enums::SAE, Enums::Wpa2Eap, Enums::Wpa2Psk, Enums::WpaEap, Enums::WpaPsk,
        Enums::StaticWep, Enums::DynamicWep, Enums::Leap, Enums::OWE, Enums::NoneSecurity,
    };

    const bool adhoc = mode == ModeAdhoc;
    for (const auto type : order) {
        if (valid(type, deviceCapabilities, adhoc, apFlags, wpaFlags, rsnFlags))
            return type;
    }
    return advertised(apFlags, wpaFlags, rsnFlags);
}

bool enterprise(Enums::SecurityType type)
{
    return type == Enums::DynamicWep || type == Enums::Leap || type == Enums::WpaEap
            || type == Enums::Wpa2Eap || type == Enums::Wpa3SuiteB192;
}

bool needsPassword(Enums::SecurityType type)
{
    return type == Enums::StaticWep || type == Enums::WpaPsk || type == Enums::Wpa2Psk
            || type == Enums::SAE;
}

QString securityLabel(Enums::SecurityType type)
{
    switch (type) {
    case Enums::NoneSecurity:
        return QObject::tr("Insecure");
    case Enums::StaticWep:
        return QObject::tr("WEP");
    case Enums::Leap:
        return QObject::tr("LEAP");
    case Enums::DynamicWep:
        return QObject::tr("Dynamic WEP");
    case Enums::WpaPsk:
        return QObject::tr("WPA-PSK");
    case Enums::WpaEap:
        return QObject::tr("WPA-EAP");
    case Enums::Wpa2Psk:
        return QObject::tr("WPA2-PSK");
    case Enums::Wpa2Eap:
        return QObject::tr("WPA2-EAP");
    case Enums::SAE:
        return QObject::tr("WPA3-SAE");
    case Enums::Wpa3SuiteB192:
        return QObject::tr("WPA3-EAP-Suite-B-192");
    case Enums::OWE:
        return QObject::tr("OWE");
    default:
        return QObject::tr("Unknown");
    }
}

int strengthStep(int strength)
{
    if (strength <= 0)
        return 0;
    if (strength <= 33)
        return 33;
    if (strength <= 66)
        return 66;
    return 100;
}

QString strengthIcon(int strength)
{
    return QStringLiteral("network-wireless-connected-%1").arg(strengthStep(strength), 2, 10, QLatin1Char('0'));
}

bool pskValid(const QString &psk)
{
    const QByteArray bytes = psk.toUtf8();
    if (bytes.size() == 64) {
        for (const char c : bytes) {
            if (!std::isxdigit(static_cast<unsigned char>(c)))
                return false;
        }
        return true;
    }
    return bytes.size() >= 8 && bytes.size() <= 63;
}

bool wepKeyValid(const QString &key)
{
    if (key.size() == 10 || key.size() == 26) {
        for (const QChar c : key) {
            if (c.unicode() > 0x7f || !std::isxdigit(c.unicode()))
                return false;
        }
        return true;
    }
    if (key.size() != 5 && key.size() != 13)
        return false;
    for (const QChar c : key) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7e)
            return false;
    }
    return true;
}

QString generatedPassword()
{
    // nmcli's alphabet, without the characters that read like others.
    static const QString alphabet =
            QStringLiteral("234679ACDEFGHJKLMNPRTUVWXYZabcdefghijkmnopqrstuvwxyz");
    QString password;
    for (int i = 0; i < 12; ++i)
        password += alphabet.at(QRandomGenerator::system()->bounded(alphabet.size()));
    return password;
}

} // namespace WifiUtils
