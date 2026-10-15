// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SSID charset table and security checks are ported from NetworkManager's
// src/libnm-core-impl/nm-utils.c (LGPL-2.1-or-later, Copyright 2005 - 2017 Red Hat, Inc.).

#include "networkmanager/wifiutils.h"

#include <algorithm>
#include <cctype>
#include <iconv.h>
#include <sys/random.h>
#include <vector>

#include "base/locale.h"

namespace cutefish::networkmanager::wifi {

struct LocaleEncodings
{
    const char* locale;
    std::vector<const char*> encodings;
};

// NetworkManager's table in libnm-core-impl/nm-utils.c, by language and then country.
static const LocaleEncodings localeEncodings[] = {
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

static std::vector<const char*> encodingsForLocale(const std::string& language)
{
    std::string name = language.empty() ? base::Locale::system().name() : language;
    name = name.substr(0, name.find_first_of(".@"));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    for (const auto& entry : localeEncodings) {
        if (name == entry.locale)
            return entry.encodings;
    }
    const std::string languageCode = name.substr(0, 2);
    for (const auto& entry : localeEncodings) {
        if (languageCode == entry.locale)
            return entry.encodings;
    }
    return {};
}

static bool decode(const std::string& bytes, const char* encoding, std::string* text)
{
    iconv_t converter = iconv_open("UTF-8", encoding);
    if (converter == iconv_t(-1))
        return false;

    std::string output(bytes.size() * 4 + 4, '\0');
    char* in = const_cast<char*>(bytes.data());
    size_t inLeft = bytes.size();
    char* out = output.data();
    size_t outLeft = output.size();
    const size_t result = iconv(converter, &in, &inLeft, &out, &outLeft);
    iconv_close(converter);
    if (result == size_t(-1) || inLeft > 0)
        return false;

    output.resize(output.size() - outLeft);
    *text = std::move(output);
    return true;
}

// The code points of valid UTF-8, or nothing when it isn't.
static bool decodeUtf8(const std::string& bytes, std::vector<char32_t>* codePoints)
{
    size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char lead = bytes[i];
        int length = 0;
        char32_t codePoint = 0;
        if (lead < 0x80) {
            length = 1;
            codePoint = lead;
        } else if ((lead & 0xe0) == 0xc0 && lead >= 0xc2) {
            length = 2;
            codePoint = lead & 0x1f;
        } else if ((lead & 0xf0) == 0xe0) {
            length = 3;
            codePoint = lead & 0x0f;
        } else if ((lead & 0xf8) == 0xf0 && lead <= 0xf4) {
            length = 4;
            codePoint = lead & 0x07;
        } else {
            return false;
        }
        if (i + length > bytes.size())
            return false;
        for (int k = 1; k < length; ++k) {
            const unsigned char next = bytes[i + k];
            if ((next & 0xc0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        // Overlong forms, surrogates and values past Unicode.
        if ((length == 3 && codePoint < 0x800) || (length == 4 && (codePoint < 0x10000 || codePoint > 0x10ffff))
            || (codePoint >= 0xd800 && codePoint <= 0xdfff))
            return false;
        if (codePoints)
            codePoints->push_back(codePoint);
        i += length;
    }
    return true;
}

static void appendUtf8(std::string& out, char32_t c)
{
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xc0 | (c >> 6));
        out += char(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
        out += char(0xe0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3f));
        out += char(0x80 | (c & 0x3f));
    } else {
        out += char(0xf0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3f));
        out += char(0x80 | ((c >> 6) & 0x3f));
        out += char(0x80 | (c & 0x3f));
    }
}

// A control character in a label would break the line it sits in.
static std::string printable(const std::string& utf8)
{
    std::vector<char32_t> codePoints;
    decodeUtf8(utf8, &codePoints);
    std::string text;
    for (const char32_t c : codePoints)
        appendUtf8(text, c < 0x20 || (c >= 0x7f && c <= 0x9f) ? char32_t(0xfffd) : c);
    return text;
}

static bool supportsCiphers(uint32_t caps, uint32_t flags, bool staticWep)
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

static bool pskCipher(uint32_t caps, uint32_t flags)
{
    return ((flags & PairTkip) && (caps & CapTkip)) || ((flags & PairCcmp) && (caps & CapCcmp));
}

// nm_utils_security_valid() for an access point that is in range.
static bool valid(SecurityType type, uint32_t caps, bool adhoc, uint32_t apFlags, uint32_t wpa, uint32_t rsn)
{
    switch (type) {
    case SecurityType::None:
        return !(apFlags & ApPrivacy) && !wpa && !rsn;
    case SecurityType::Leap:
        if (adhoc)
            return false;
        [[fallthrough]];
    case SecurityType::StaticWep:
        if (!(apFlags & ApPrivacy))
            return false;
        if (wpa || rsn)
            return supportsCiphers(caps, wpa, true) || supportsCiphers(caps, rsn, true);
        return true;
    case SecurityType::DynamicWep:
        if (adhoc || rsn || !(apFlags & ApPrivacy))
            return false;
        // Some access points broadcast minimal WPA-enabled beacons.
        if (wpa)
            return (wpa & KeyMgmt8021x) && supportsCiphers(caps, wpa, false);
        return true;
    case SecurityType::WpaPsk:
        return !adhoc && (caps & CapWpa) && (wpa & KeyMgmtPsk) && pskCipher(caps, wpa);
    case SecurityType::Wpa2Psk:
        if (!(caps & CapRsn))
            return false;
        if (adhoc)
            return (caps & CapIbssRsn) && (rsn & PairCcmp) && (caps & CapCcmp);
        return (rsn & KeyMgmtPsk) && pskCipher(caps, rsn);
    case SecurityType::WpaEap:
        return !adhoc && (caps & CapWpa) && (wpa & KeyMgmt8021x) && supportsCiphers(caps, wpa, false);
    case SecurityType::Wpa2Eap:
        return !adhoc && (caps & CapRsn) && (rsn & KeyMgmt8021x) && supportsCiphers(caps, rsn, false);
    case SecurityType::Sae:
        return !adhoc && (caps & CapRsn) && (rsn & KeyMgmtSae) && (rsn & PairCcmp) && (caps & CapCcmp);
    case SecurityType::Owe:
        return !adhoc && (caps & CapRsn) && (rsn & (KeyMgmtOwe | KeyMgmtOweTm));
    case SecurityType::Wpa3SuiteB192:
        return !adhoc && (caps & CapRsn) && (rsn & KeyMgmtEapSuiteB192);
    default:
        return false;
    }
}

// What the access point advertises, for devices that report no capabilities.
static SecurityType advertised(uint32_t apFlags, uint32_t wpa, uint32_t rsn)
{
    if (rsn & KeyMgmtEapSuiteB192)
        return SecurityType::Wpa3SuiteB192;
    if (rsn & KeyMgmtSae)
        return SecurityType::Sae;
    if (rsn & KeyMgmt8021x)
        return SecurityType::Wpa2Eap;
    if (rsn & KeyMgmtPsk)
        return SecurityType::Wpa2Psk;
    if (wpa & KeyMgmt8021x)
        return SecurityType::WpaEap;
    if (wpa & KeyMgmtPsk)
        return SecurityType::WpaPsk;
    if (rsn & (KeyMgmtOwe | KeyMgmtOweTm))
        return SecurityType::Owe;
    if (apFlags & ApPrivacy)
        return SecurityType::StaticWep;
    return SecurityType::None;
}

std::string ssidLabel(const std::string& ssid, const std::string& language)
{
    const std::string bytes = ssidKey(ssid);
    if (decodeUtf8(bytes, nullptr))
        return printable(bytes);

    for (const char* encoding : encodingsForLocale(language)) {
        std::string decoded;
        if (decode(bytes, encoding, &decoded))
            return printable(decoded);
    }

    std::string latin1;
    for (const unsigned char c : bytes)
        appendUtf8(latin1, c);
    return printable(latin1);
}

bool ssidHidden(const std::string& ssid)
{
    if (ssid == " ")
        return true;
    return std::all_of(ssid.begin(), ssid.end(), [](char c) { return c == '\0'; });
}

std::string ssidKey(const std::string& ssid)
{
    if (!ssid.empty() && ssid.back() == '\0')
        return ssid.substr(0, ssid.size() - 1);
    return ssid;
}

std::string ssidBytes(const std::string& name)
{
    if (name.size() <= 32)
        return name;
    size_t size = 32;
    while (size > 0 && (static_cast<unsigned char>(name[size]) & 0xc0) == 0x80)
        --size;
    return name.substr(0, size);
}

SecurityType security(uint32_t deviceCapabilities, uint32_t apFlags, uint32_t wpaFlags, uint32_t rsnFlags,
                      uint32_t mode)
{
    // Security level first, then how common each is.
    static const SecurityType order[] = {
        SecurityType::Wpa3SuiteB192, SecurityType::Sae,       SecurityType::Wpa2Eap,    SecurityType::Wpa2Psk,
        SecurityType::WpaEap,        SecurityType::WpaPsk,    SecurityType::StaticWep,  SecurityType::DynamicWep,
        SecurityType::Leap,          SecurityType::Owe,       SecurityType::None,
    };

    const bool adhoc = mode == ModeAdhoc;
    for (const auto type : order) {
        if (valid(type, deviceCapabilities, adhoc, apFlags, wpaFlags, rsnFlags))
            return type;
    }
    return advertised(apFlags, wpaFlags, rsnFlags);
}

bool enterprise(SecurityType type)
{
    return type == SecurityType::DynamicWep || type == SecurityType::Leap || type == SecurityType::WpaEap
           || type == SecurityType::Wpa2Eap || type == SecurityType::Wpa3SuiteB192;
}

bool needsPassword(SecurityType type)
{
    return type == SecurityType::StaticWep || type == SecurityType::WpaPsk || type == SecurityType::Wpa2Psk
           || type == SecurityType::Sae;
}

std::string securityLabel(SecurityType type)
{
    switch (type) {
    case SecurityType::None:
        return "Insecure";
    case SecurityType::StaticWep:
        return "WEP";
    case SecurityType::Leap:
        return "LEAP";
    case SecurityType::DynamicWep:
        return "Dynamic WEP";
    case SecurityType::WpaPsk:
        return "WPA-PSK";
    case SecurityType::WpaEap:
        return "WPA-EAP";
    case SecurityType::Wpa2Psk:
        return "WPA2-PSK";
    case SecurityType::Wpa2Eap:
        return "WPA2-EAP";
    case SecurityType::Sae:
        return "WPA3-SAE";
    case SecurityType::Wpa3SuiteB192:
        return "WPA3-EAP-Suite-B-192";
    case SecurityType::Owe:
        return "OWE";
    default:
        return "Unknown";
    }
}

base::dbus::VariantMap passwordEap(const std::string& identity, const std::string& password, uint32_t passwordFlags)
{
    return {{"eap", base::dbus::Variant(std::vector<std::string>{"peap", "ttls"})},
            {"phase2-auth", base::dbus::Variant(std::string("mschapv2"))},
            {"identity", base::dbus::Variant(identity)},
            {"password", base::dbus::Variant(password)},
            {"password-flags", base::dbus::Variant(passwordFlags)}};
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

std::string strengthIcon(int strength)
{
    const int step = strengthStep(strength);
    return "network-wireless-connected-" + std::string(step < 10 ? "0" : "") + std::to_string(step);
}

bool pskValid(const std::string& psk)
{
    if (psk.size() == 64)
        return std::all_of(psk.begin(), psk.end(), [](unsigned char c) { return std::isxdigit(c); });
    return psk.size() >= 8 && psk.size() <= 63;
}

std::string generatedPassword()
{
    // nmcli's alphabet, without the characters that read like others.
    static const std::string alphabet = "234679ACDEFGHJKLMNPRTUVWXYZabcdefghijkmnopqrstuvwxyz";
    std::string password;
    while (password.size() < 12) {
        unsigned char random[16];
        if (getrandom(random, sizeof(random), 0) != ssize_t(sizeof(random)))
            continue;
        for (const unsigned char byte : random) {
            // Rejection sampling keeps every character equally likely.
            const unsigned limit = 256 - 256 % alphabet.size();
            if (byte < limit && password.size() < 12)
                password += alphabet[byte % alphabet.size()];
        }
    }
    return password;
}

} // namespace cutefish::networkmanager::wifi
