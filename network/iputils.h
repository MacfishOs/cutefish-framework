// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "networkdbustypes.h"

#include <network_export.h>

#include <QString>
#include <QVariantMap>

class NetworkClient;

namespace IpUtils {

// A profile's addressing as its form shows it: ipv4Method, ipv4Address, ipv4Netmask,
// ipv4Gateway, ipv6Method, ipv6Address, ipv6Prefix, ipv6Gateway, automaticDns, and dns as
// comma-separated text. Methods are NetworkManager's; ipv4Methods and ipv6Methods list the
// choices as {value, label}, with a method the form doesn't offer kept when the profile has it.
// Only the first address of a family is shown.
CUTEFISH_NETWORK_EXPORT QVariantMap form(const NMVariantMapMap &settings);
// The addresses an active profile has now, in the form's keys, for filling in a manual form.
CUTEFISH_NETWORK_EXPORT QVariantMap currentAddresses(const NetworkClient *client, const QString &connection);

// What the form lacks or gets wrong: {complete, message}. A required value left empty only
// makes it incomplete; a message says what is wrong with a value that was typed.
CUTEFISH_NETWORK_EXPORT QVariantMap check(const QVariantMap &form);
// The profile with the form's addressing. Only the parts of the form that differ from the
// profile are written, so whatever the form doesn't show stays as it is. Empty with `error`
// set when the form is invalid; `changed` says whether anything differs.
CUTEFISH_NETWORK_EXPORT NMVariantMapMap settings(const QVariantMap &form, const NMVariantMapMap &existing,
                                                 bool *changed, QString *error);

} // namespace IpUtils
