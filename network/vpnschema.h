// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "networkdbustypes.h"

#include <network_export.h>

#include <QList>
#include <QMap>
#include <QString>
#include <QVariantMap>

// What each kind of NetworkManager VPN plugin can be told, as the GNOME and KDE editors
// lay it out: a field per vpn.data key, or per vpn.secrets key with its `-flags` entry in
// vpn.data. The forms in Settings are drawn from these tables.
namespace VpnSchema {

enum class Type {
    Text,
    Password, // A vpn.secrets key, kept as its storage choice says.
    File,
    Choice,
    Bool,
    Number,
};

struct Choice {
    QString value;
    QString label;
};

struct Field {
    QString key;
    QString label;
    Type type = Type::Text;
    // Advanced fields sit behind "Show advanced options", grouped by section.
    bool advanced = false;
    QString section;
    bool required = false;
    QString placeholder;
    QList<Choice> choices;
    // The value an absent key stands for: a Choice's default, a Number's placeholder.
    QString defaultValue;
    // Shown only while another field has one of these values: "key=a|b".
    QString showWhen;
    // Bools: what is written for true (removed for false, unless falseValue is set).
    QString trueValue = QStringLiteral("yes");
    QString falseValue;
    // Bools whose key says the opposite of the label, e.g. "nobsdcomp" for "BSD compression".
    bool inverted = false;
    // Bools that set other keys along with their own, e.g. the LCP echo interval and failure count.
    QMap<QString, QString> alsoSet;
    // Choices whose values are keys: the chosen one is set to "yes", the others removed.
    bool exclusiveKeys = false;
    // Passwords the plugin can do without, as a storage choice.
    bool allowNotRequired = false;
    // A legacy key some plugins keep next to the flags: save / ask / unused.
    QString legacyTypeKey;
    int minimum = 0;
    int maximum = 0;
};

// Every kind a form exists for, in the order the Add VPN list shows them; "wireguard" has its
// own form and isn't here.
CUTEFISH_NETWORK_EXPORT QStringList kinds();
CUTEFISH_NETWORK_EXPORT QList<Field> fields(const QString &kind);
// Where the plugin's reader and writer can take a file from and to, as nmcli's import and
// export do: name filters like "*.ovpn", empty when the plugin has none.
CUTEFISH_NETWORK_EXPORT QStringList importPatterns(const QString &kind);
CUTEFISH_NETWORK_EXPORT bool exportable(const QString &kind);

// The fields of a kind with their values from a profile's vpn.data, as the form shows
// them: [{key, label, type, advanced, section, required, placeholder, showWhen, trueValue,
// allowNotRequired, minimum, maximum, value, storage, choices}]. Bools come out as bool,
// with inverted ones already turned round; passwords come out empty, with `storage` set to
// their flags.
CUTEFISH_NETWORK_EXPORT QVariantList formFields(const QString &kind, const QMap<QString, QString> &data);

// vpn.data and vpn.secrets after the form's values are written over the existing ones; keys
// the form doesn't know stay. `values` holds the form's value per key (bool for Bools),
// `storage` the flags chosen per password, `secrets` the passwords typed, which with
// `shared` set are system-owned rather than this user's. Empty with `error` set when a
// required field is empty.
CUTEFISH_NETWORK_EXPORT bool apply(const QString &kind, const QVariantMap &values, const QVariantMap &storage,
                                   const QVariantMap &secrets, bool shared, QMap<QString, QString> &data,
                                   QMap<QString, QString> &vpnSecrets, QString *error);

} // namespace VpnSchema
