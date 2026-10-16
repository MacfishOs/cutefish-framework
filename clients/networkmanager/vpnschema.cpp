// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "networkmanager/vpnschema.h"
#include "networkmanager/text.h"
#include "networkmanager/types.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace cutefish::networkmanager::vpn::schema {

namespace {

// Builds one kind's table; the helpers keep each field to a line.
struct Table
{
    std::vector<Field> fields;
    std::string section;
    bool advanced = false;

    Field& add(const std::string& key, const std::string& label, Type type)
    {
        Field field;
        field.key = key;
        field.label = label;
        field.type = type;
        field.advanced = advanced;
        field.section = section;
        fields.push_back(field);
        return fields.back();
    }

    Field& text(const std::string& key, const std::string& label) { return add(key, label, Type::Text); }
    Field& file(const std::string& key, const std::string& label) { return add(key, label, Type::File); }
    Field& flag(const std::string& key, const std::string& label) { return add(key, label, Type::Bool); }

    Field& password(const std::string& key, const std::string& label, bool allowNotRequired = false)
    {
        Field& field = add(key, label, Type::Password);
        field.allowNotRequired = allowNotRequired;
        return field;
    }

    Field& number(const std::string& key, const std::string& label, int minimum, int maximum,
                  const std::string& defaultValue = {})
    {
        Field& field = add(key, label, Type::Number);
        field.minimum = minimum;
        field.maximum = maximum;
        field.defaultValue = defaultValue;
        field.placeholder = defaultValue;
        return field;
    }

    Field& choice(const std::string& key, const std::string& label, const std::vector<Choice>& choices,
                  const std::string& defaultValue = {})
    {
        Field& field = add(key, label, Type::Choice);
        field.choices = choices;
        field.defaultValue = defaultValue;
        return field;
    }

    // The PPP options the pppd-based plugins share.
    void ppp(bool authentication = true)
    {
        section = "Authentication methods";
        if (authentication) {
            flag("refuse-pap", "PAP").inverted = true;
            flag("refuse-chap", "CHAP").inverted = true;
            flag("refuse-mschap", "MSCHAP").inverted = true;
            flag("refuse-mschapv2", "MSCHAPv2").inverted = true;
            flag("refuse-eap", "EAP").inverted = true;
        }
        section = "Encryption and compression";
        choice("require-mppe", "Point-to-point encryption",
               {{"", "Not required"},
                {"require-mppe", "Any"},
                {"require-mppe-128", "128-bit"},
                {"require-mppe-40", "40-bit"}})
            .exclusiveKeys = true;
        flag("mppe-stateful", "Stateful encryption");
        flag("nobsdcomp", "BSD compression").inverted = true;
        flag("nodeflate", "Deflate compression").inverted = true;
        flag("no-vj-comp", "TCP header compression").inverted = true;
        section = "Link";
        Field& echo = flag("lcp-echo-interval", "Send PPP echo packets");
        echo.trueValue = "30";
        echo.alsoSet["lcp-echo-failure"] = "5";
    }
};

} // namespace

static std::vector<Choice> tlsVersions()
{
    return {{"", "Default"},
            {"1.0", "1.0"},
            {"1.1", "1.1"},
            {"1.2", "1.2"},
            {"1.3", "1.3"}};
}

static std::vector<Choice> dhGroups(const std::string& defaultLabel, const std::vector<std::string>& extra = {})
{
    std::vector<Choice> choices{{"", defaultLabel}};
    for (const std::string& value : extra)
        choices.push_back(Choice{value, value});
    for (const char* group : {"dh1", "dh2", "dh5", "dh14", "dh15", "dh16", "dh17", "dh18"})
        choices.push_back(Choice{group, std::string("DH group ") + (group + 2)});
    return choices;
}

static Table openvpn()
{
    Table t;
    t.text("remote", "Server").required = true;
    t.fields.back().placeholder = "host[:port[:proto]], several separated by commas";
    t.choice("connection-type", "Sign in with",
             {{"password", "User name and password"},
              {"tls", "Certificates"},
              {"password-tls", "User name, password and certificates"},
              {"static-key", "Static key"}},
             "tls");
    const std::string withPassword = "connection-type=password|password-tls";
    const std::string withCertificates = "connection-type=tls|password-tls";
    t.text("username", "User name").showWhen = withPassword;
    t.password("password", "Password", true).showWhen = withPassword;
    t.file("ca", "CA certificate").showWhen = "connection-type=tls|password|password-tls";
    t.file("cert", "User certificate").showWhen = withCertificates;
    t.file("key", "Private key").showWhen = withCertificates;
    t.password("cert-pass", "Private key password", true).showWhen = withCertificates;
    t.file("static-key", "Static key").showWhen = "connection-type=static-key";
    t.choice("static-key-direction", "Key direction",
             {{"", "None"}, {"0", "0"}, {"1", "1"}})
            .showWhen = "connection-type=static-key";
    t.text("remote-ip", "Remote IP address").showWhen = "connection-type=static-key";
    t.text("local-ip", "Local IP address").showWhen = "connection-type=static-key";

    t.advanced = true;
    t.section = "General";
    t.number("port", "Port", 1, 65535, "1194");
    t.flag("proto-tcp", "Use TCP");
    t.choice("dev-type", "Device type",
             {{"", "Automatic"}, {"tun", "TUN"}, {"tap", "TAP"}});
    t.text("dev", "Device name");
    t.number("tunnel-mtu", "Tunnel MTU", 1, 65535, "1500");
    t.number("fragment-size", "UDP fragment size", 1, 65535, "1300");
    t.flag("mssfix", "Restrict TCP maximum segment size");
    t.choice("mtu-disc", "Path MTU discovery",
             {{"", "Default"}, {"no", "Never"},
              {"maybe", "When possible"}, {"yes", "Always"}});
    t.number("reneg-seconds", "Renegotiate every (seconds)", 0, 604800, "0");
    t.choice("compress", "Compression",
             {{"", "None"}, {"lzo", "LZO"},
              {"lz4", "LZ4"}, {"lz4-v2", "LZ4 v2"},
              {"yes", "Automatic"}});
    t.choice("allow-compression", "Allow compression",
             {{"", "Default"}, {"asym", "Incoming only"},
              {"yes", "Both ways"}, {"no", "No"}});
    t.flag("remote-random", "Pick servers at random");
    t.flag("remote-random-hostname", "Randomise server host names");
    t.flag("allow-pull-fqdn", "Allow pulled host names");
    t.flag("tun-ipv6", "IPv6 inside the tunnel");
    t.flag("float", "Accept the server from any address");
    t.flag("push-peer-info", "Send client details to the server");
    t.number("ping", "Ping every (seconds)", 1, 65535, "30");
    t.number("ping-exit", "Exit without a ping after (seconds)", 1, 65535, "30");
    t.number("ping-restart", "Restart without a ping after (seconds)", 1, 65535, "30");
    t.number("connect-timeout", "Connection timeout (seconds)", 1, 65535, "120");
    t.number("max-routes", "Maximum routes", 1, 65535, "100");

    t.section = "Security";
    t.text("cipher", "Cipher").placeholder = "Default";
    t.text("data-ciphers", "Data ciphers").placeholder = "Default";
    t.text("data-ciphers-fallback", "Data cipher fallback").placeholder = "Default";
    t.flag("ncp-disable", "Disable cipher negotiation");
    std::vector<Choice> hmac{{"", "Default"}, {"none", "None"}};
    for (const char* name : {"MD5", "SHA1", "SHA224", "SHA256", "SHA384", "SHA512", "RIPEMD160", "RSA-MD4"})
        hmac.push_back(Choice{name, name});
    t.choice("auth", "HMAC authentication", hmac);
    t.number("keysize", "Key size (bits)", 1, 65535, "128");
    t.file("crl-verify-file", "Revocation list file");
    t.file("crl-verify-dir", "Revocation list directory");

    t.section = "TLS";
    t.text("verify-x509-name", "Server certificate name").placeholder
            = "name:vpn.example.com, subject:… or name-prefix:…";
    t.choice("remote-cert-tls", "Server certificate usage",
             {{"", "Not checked"}, {"server", "Server"}, {"client", "Client"}});
    t.choice("ns-cert-type", "Netscape certificate type",
             {{"", "Not checked"}, {"server", "Server"}, {"client", "Client"}});
    t.file("ta", "TLS authentication key");
    t.choice("ta-dir", "TLS key direction",
             {{"", "None"}, {"0", "0"}, {"1", "1"}});
    t.file("tls-crypt", "TLS encryption key");
    t.file("tls-crypt-v2", "TLS encryption key (v2)");
    t.file("extra-certs", "Extra certificates");
    t.text("tls-cipher", "TLS cipher").placeholder = "Default";
    t.choice("tls-version-min", "Minimum TLS version", tlsVersions());
    t.flag("tls-version-min-or-highest", "Or the highest the server supports");
    t.choice("tls-version-max", "Maximum TLS version", tlsVersions());

    t.section = "Proxy";
    t.choice("proxy-type", "Proxy",
             {{"", "None"}, {"http", "HTTP"}, {"socks", "SOCKS"}});
    const std::string withProxy = "proxy-type=http|socks";
    t.text("proxy-server", "Proxy server").showWhen = withProxy;
    t.number("proxy-port", "Proxy port", 1, 65535).showWhen = withProxy;
    t.flag("proxy-retry", "Retry indefinitely").showWhen = withProxy;
    t.text("http-proxy-username", "Proxy user name").showWhen = "proxy-type=http";
    t.password("http-proxy-password", "Proxy password", true).showWhen = "proxy-type=http";
    return t;
}

static Table vpnc()
{
    Table t;
    t.text("IPSec gateway", "Server").required = true;
    t.text("IPSec ID", "Group name").required = true;
    t.text("Xauth username", "User name");
    Field& user = t.password("Xauth password", "Password", true);
    user.legacyTypeKey = "xauth-password-type";
    Field& group = t.password("IPSec secret", "Group password", true);
    group.legacyTypeKey = "ipsec-secret-type";
    t.flag("IKE Authmode", "Hybrid authentication").trueValue = "hybrid";
    t.file("CA-File", "CA certificate").showWhen = "IKE Authmode=hybrid";

    t.advanced = true;
    t.section = "General";
    t.text("Domain", "Domain");
    t.choice("Vendor", "Vendor",
             {{"", "Cisco"}, {"netscreen", "Netscreen"},
              {"fortigate", "Fortigate"}});
    t.text("Application Version", "Application version");
    t.number("Interface MTU", "Interface MTU", 1200, 12000);
    t.section = "Security";
    t.flag("Enable Single DES", "Weak encryption (single DES)");
    t.flag("Enable no encryption", "No encryption");
    t.flag("Enable weak authentication", "Weak authentication");
    t.flag("Enable weak encryption", "Weak encryption");
    t.choice("NAT Traversal Mode", "NAT traversal",
             {{"", "NAT-T when detected"}, {"force-natt", "NAT-T always"},
              {"cisco-udp", "Cisco UDP"}, {"none", "Off"}});
    t.choice("IKE DH Group", "IKE DH group", dhGroups("DH group 2"));
    t.choice("Perfect Forward Secrecy", "Perfect forward secrecy",
             dhGroups("As the server says", {"nopfs"}));
    t.number("Local Port", "Local port", 0, 65535, "0");
    t.number("DPD idle timeout (our side)", "Dead peer detection (seconds, 0 is off)", 0, 86400, "90");
    return t;
}

static Table l2tp()
{
    Table t;
    t.text("gateway", "Server").required = true;
    t.choice("user-auth-type", "Sign in with",
             {{"", "User name and password"}, {"tls", "Certificates"}});
    const std::string withPassword = "user-auth-type=";
    const std::string withCertificates = "user-auth-type=tls";
    t.text("user", "User name").showWhen = withPassword;
    t.password("password", "Password", true).showWhen = withPassword;
    t.text("domain", "NT domain").showWhen = withPassword;
    t.file("user-ca", "CA certificate").showWhen = withCertificates;
    t.file("user-cert", "User certificate").showWhen = withCertificates;
    t.file("user-key", "Private key").showWhen = withCertificates;
    t.password("user-certpass", "Private key password", true).showWhen = withCertificates;

    t.advanced = true;
    t.section = "IPsec";
    t.flag("ipsec-enabled", "IPsec tunnel to the server");
    const std::string withIpsec = "ipsec-enabled=yes";
    t.text("ipsec-remote-id", "Server identity").showWhen = withIpsec;
    t.choice("machine-auth-type", "Machine authentication",
             {{"", "Pre-shared key"}, {"tls", "Certificates"}})
            .showWhen = withIpsec;
    t.password("ipsec-psk", "Pre-shared key", true).showWhen = "machine-auth-type=";
    t.file("machine-ca", "Machine CA certificate").showWhen = "machine-auth-type=tls";
    t.file("machine-cert", "Machine certificate").showWhen = "machine-auth-type=tls";
    t.file("machine-key", "Machine private key").showWhen = "machine-auth-type=tls";
    t.password("machine-certpass", "Machine key password", true).showWhen = "machine-auth-type=tls";
    t.text("ipsec-ike", "Phase 1 algorithms").showWhen = withIpsec;
    t.text("ipsec-esp", "Phase 2 algorithms").showWhen = withIpsec;
    t.number("ipsec-ikelifetime", "Phase 1 lifetime (seconds)", 0, 1000000).showWhen = withIpsec;
    t.number("ipsec-salifetime", "Phase 2 lifetime (seconds)", 0, 1000000).showWhen = withIpsec;
    t.flag("ipsec-forceencaps", "Force UDP encapsulation").showWhen = withIpsec;
    t.flag("ipsec-ipcomp", "IP compression").showWhen = withIpsec;
    t.flag("ipsec-ikev2", "Use IKEv2").showWhen = withIpsec;
    t.flag("ipsec-pfs", "Disable perfect forward secrecy").showWhen = withIpsec;
    t.fields.back().trueValue = "no";
    t.ppp();
    t.section = "Link";
    t.number("mtu", "MTU", 575, 1500, "1400");
    t.number("mru", "MRU", 575, 1500, "1400");
    t.number("mrru", "Multilink MRRU", 1500, 4500);
    t.flag("ephemeral-port", "Use a random source port");
    return t;
}

static Table pptp()
{
    Table t;
    t.text("gateway", "Server").required = true;
    t.text("user", "User name");
    t.password("password", "Password", true);
    t.text("domain", "NT domain");
    t.advanced = true;
    t.ppp();
    t.number("unit", "PPP unit number", 0, 65535);
    return t;
}

static Table sstp()
{
    Table t;
    t.text("gateway", "Server").required = true;
    t.fields.back().placeholder = "host[:port]";
    t.choice("connection-type", "Sign in with",
             {{"", "User name and password"}, {"tls", "Certificates"}});
    const std::string withPassword = "connection-type=";
    const std::string withCertificates = "connection-type=tls";
    t.text("user", "User name").showWhen = withPassword;
    t.password("password", "Password", true).showWhen = withPassword;
    t.text("domain", "NT domain").showWhen = withPassword;
    t.file("tls-ca-cert", "CA certificate").showWhen = withCertificates;
    t.file("tls-user-cert", "User certificate").showWhen = withCertificates;
    t.file("tls-user-key", "Private key").showWhen = withCertificates;
    t.password("tls-user-key-secret", "Private key password", true).showWhen = withCertificates;

    t.advanced = true;
    t.section = "Certificates";
    t.file("ca-cert", "Server CA certificate");
    t.file("crl-file", "Revocation list");
    t.flag("ignore-cert-warn", "Ignore certificate warnings");
    t.flag("tls-ext", "TLS hostname extension").falseValue = "no";
    t.fields.back().defaultValue = "yes";
    t.section = "TLS";
    t.text("tls-identity", "Identity");
    t.choice("tls-verify-method", "Verify the server name by",
             {{"", "Default"}, {"none", "Nothing"},
              {"subject", "Subject"}, {"name", "Name"},
              {"suffix", "Name suffix"}});
    t.text("tls-remotename", "Expected server name");
    t.flag("tls-verify-key-usage", "Check key usage");
    t.choice("tls-max-version", "Maximum TLS version", tlsVersions());
    t.ppp();
    for (Field& field : t.fields) {
        if (field.key == "refuse-pap" || field.key == "refuse-chap")
            field.defaultValue = "yes";
    }
    t.section = "Link";
    t.number("unit", "PPP unit number", 0, 65535);
    t.number("mtu", "MTU", 1, 65535);
    t.section = "Proxy";
    t.text("proxy-server", "Proxy server");
    t.number("proxy-port", "Proxy port", 1, 65535);
    t.text("proxy-user", "Proxy user name");
    t.password("proxy-password", "Proxy password", true);
    return t;
}

static Table strongswan()
{
    Table t;
    t.text("address", "Server").required = true;
    t.file("certificate", "Server CA certificate");
    t.text("remote-identity", "Server identity");
    t.choice("method", "Sign in with",
             {{"eap", "User name and password (EAP)"},
              {"psk", "Pre-shared key"},
              {"cert", "Certificate"},
              {"eap-tls", "Certificate (EAP-TLS)"},
              {"key", "Certificate and private key"},
              {"agent", "Certificate and SSH agent"},
              {"smartcard", "Smart card"}},
             "eap");
    t.text("user", "User name").showWhen = "method=eap|psk";
    t.choice("cert-source", "Certificate from",
             {{"", "A file"}, {"agent", "The SSH agent"},
              {"smartcard", "A smart card"}})
            .showWhen = "method=cert|eap-tls";
    t.file("usercert", "User certificate").showWhen = "method=cert|eap-tls|key|agent";
    t.file("userkey", "Private key").showWhen = "method=cert|eap-tls|key";
    t.password("password", "Password", true).showWhen = "method=eap|psk|cert|eap-tls|key|smartcard";
    t.text("local-identity", "Client identity");
    t.advanced = true;
    t.section = "Options";
    t.number("server-port", "Server port", 1, 65535, "500");
    t.flag("virtual", "Request an inner IP address");
    t.flag("encap", "Enforce UDP encapsulation");
    t.flag("ipcomp", "IP compression");
    t.flag("proposal", "Custom algorithm proposals");
    t.text("ike", "IKE proposal").showWhen = "proposal=yes";
    t.text("esp", "ESP proposal").showWhen = "proposal=yes";
    t.text("local-ts", "Local traffic selectors");
    t.text("remote-ts", "Remote traffic selectors");
    return t;
}

static Table libreswan()
{
    Table t;
    t.text("right", "Server").required = true;
    t.choice("ikev2", "Sign in with",
             {{"", "Group password and user name (IKEv1)"},
              {"insist", "Certificate (IKEv2)"}});
    const std::string xauth = "ikev2=";
    const std::string certificate = "ikev2=insist";
    t.text("leftid", "Group name").showWhen = xauth;
    Field& group = t.password("pskvalue", "Group password", true);
    group.showWhen = xauth;
    group.legacyTypeKey = "pskinputmodes";
    t.text("leftxauthusername", "User name").showWhen = xauth;
    Field& user = t.password("xauthpassword", "Password", true);
    user.showWhen = xauth;
    user.legacyTypeKey = "xauthpasswordinputmodes";
    t.text("leftcert", "Certificate name").showWhen = certificate;
    t.text("rightid", "Server identity");
    t.advanced = true;
    t.section = "Security";
    t.text("ike", "Phase 1 algorithms");
    t.text("esp", "Phase 2 algorithms");
    t.text("ikelifetime", "Phase 1 lifetime");
    t.text("salifetime", "Phase 2 lifetime");
    t.flag("rekey", "Disable rekeying").trueValue = "no";
    t.flag("pfs", "Disable perfect forward secrecy").trueValue = "no";
    t.choice("authby", "Authenticate by",
             {{"", "Default"}, {"secret", "Pre-shared key"},
              {"rsasig", "RSA signature"}, {"null", "None"}});
    t.section = "Networks";
    t.text("Domain", "Domain");
    t.text("leftsubnet", "Local subnet");
    t.text("rightsubnet", "Remote subnet");
    t.flag("narrowing", "Allow narrowing");
    t.flag("mobike", "Mobility (MOBIKE)");
    t.flag("leftmodecfgclient", "Don't ask the server for an address").trueValue = "no";
    t.choice("fragmentation", "IKE fragmentation",
             {{"", "Default"}, {"no", "Off"}, {"force", "Forced"}});
    t.text("ipsec-interface", "IPsec interface");
    t.section = "Dead peer detection";
    t.number("dpddelay", "Delay (seconds)", 0, 86400);
    t.number("dpdtimeout", "Timeout (seconds)", 0, 86400);
    t.choice("dpdaction", "Action",
             {{"", "Default"}, {"hold", "Hold"},
              {"clear", "Clear"}, {"restart", "Restart"}});
    t.section = "Certificates";
    t.text("rightcert", "Server certificate");
    t.text("rightca", "Server CA");
    t.choice("leftsendcert", "Send the certificate",
             {{"", "Default"}, {"always", "Always"},
              {"ifasked", "If asked"}, {"never", "Never"}});
    t.flag("require-id-on-certificate", "Accept certificates without the identity").trueValue = "no";
    return t;
}

static Table fortisslvpn()
{
    Table t;
    t.text("gateway", "Server").required = true;
    t.text("user", "User name");
    t.password("password", "Password", true);
    t.file("ca", "CA certificate");
    t.file("cert", "User certificate");
    t.file("key", "Private key");
    t.advanced = true;
    t.section = "Options";
    t.text("trusted-cert", "Trusted certificate fingerprint");
    t.text("realm", "Realm");
    Field& otp = t.flag("otp-flags", "Ask for a one-time password");
    otp.trueValue = std::to_string(SecretNotSaved);
    Field& tfa = t.flag("2fa-flags", "Two-factor authentication");
    tfa.trueValue = std::to_string(SecretAgentOwned);
    return t;
}

static Table ssh()
{
    Table t;
    t.text("remote", "Server").required = true;
    t.choice("auth-type", "Sign in with",
             {{"", "The SSH agent"}, {"password", "Password"},
              {"key", "Key file"}});
    t.password("password", "Password", true).showWhen = "auth-type=password";
    t.file("key-file", "Key file").showWhen = "auth-type=key";
    t.text("remote-ip", "Remote IP address").required = true;
    t.text("local-ip", "Local IP address").required = true;
    t.text("netmask", "Netmask").required = true;
    t.flag("ip-6", "IPv6 addresses too");
    t.text("remote-ip-6", "Remote IPv6 address").showWhen = "ip-6=yes";
    t.text("local-ip-6", "Local IPv6 address").showWhen = "ip-6=yes";
    t.text("netmask-6", "IPv6 prefix").showWhen = "ip-6=yes";
    t.advanced = true;
    t.section = "Options";
    t.number("port", "Port", 1, 65535, "22");
    t.number("tunnel-mtu", "Tunnel MTU", 1, 65535, "1500");
    t.text("remote-username", "Remote user name").placeholder = "root";
    t.number("remote-dev", "Remote device number", 0, 255, "100");
    t.flag("tap-dev", "TAP device");
    t.flag("no-default-route", "No default route");
    t.text("extra-opts", "Extra SSH options");
    return t;
}

static Table iodine()
{
    Table t;
    t.text("topdomain", "Tunnel domain").required = true;
    t.text("nameserver", "Name server");
    t.password("password", "Password");
    t.advanced = true;
    t.section = "Options";
    t.number("fragsize", "Fragment size", 0, 65535);
    return t;
}

static Table openconnect()
{
    Table t;
    t.choice("protocol", "Protocol",
             {{"", "Cisco AnyConnect or openconnect"},
              {"nc", "Juniper Network Connect"},
              {"gp", "Palo Alto GlobalProtect"},
              {"pulse", "Pulse Connect Secure"},
              {"f5", "F5 BIG-IP"},
              {"fortinet", "Fortinet SSL VPN"},
              {"array", "Array Networks"}});
    t.text("gateway", "Server").required = true;
    t.file("cacert", "CA certificate");
    t.file("usercert", "User certificate");
    t.file("userkey", "Private key");
    t.advanced = true;
    t.section = "Options";
    t.text("proxy", "Proxy").placeholder = "http://… or socks://…";
    t.text("useragent", "User agent");
    t.choice("reported_os", "Reported operating system",
             {{"", "Default"}, {"linux", "Linux"},
              {"linux-64", "Linux 64-bit"}, {"win", "Windows"},
              {"mac-intel", "Mac"}, {"android", "Android"},
              {"apple-ios", "iOS"}});
    t.flag("disable_udp", "Disable UDP (DTLS)");
    t.flag("prevent_invalid_cert", "Refuse invalid server certificates");
    t.flag("pem_passphrase_fsid", "Private key passphrase from the file system ID");
    t.section = "Trojan";
    t.flag("enable_csd_trojan", "Run the server's compliance script");
    t.file("csd_wrapper", "Wrapper script").showWhen = "enable_csd_trojan=yes";
    t.section = "Machine certificate";
    t.file("mcacert", "Machine certificate");
    t.file("mcakey", "Machine private key");
    t.section = "Software token";
    t.choice("stoken_source", "Token",
             {{"", "None"}, {"stokenrc", "RSA SecurID from ~/.stokenrc"},
              {"manual", "RSA SecurID"}, {"totp", "TOTP"},
              {"hotp", "HOTP"}, {"yubioath", "Yubikey OATH"}});
    t.password("stoken_string", "Token secret").showWhen = "stoken_source=manual|totp|hotp|yubioath";
    return t;
}

static const std::map<std::string, Table>& tables()
{
    static const std::map<std::string, Table> tables{
        {"openvpn", openvpn()},
        {"openconnect", openconnect()},
        {"vpnc", vpnc()},
        {"l2tp", l2tp()},
        {"pptp", pptp()},
        {"sstp", sstp()},
        {"strongswan", strongswan()},
        {"libreswan", libreswan()},
        {"fortisslvpn", fortisslvpn()},
        {"ssh", ssh()},
        {"iodine", iodine()},
    };
    return tables;
}

static bool toBool(const std::string& value)
{
    // Text is true unless empty, "0" or "false" in any case.
    return !value.empty() && value != "0" && text::lower(value) != "false";
}

static std::string legacyType(uint32_t flags)
{
    if (flags & SecretNotRequired)
        return "unused";
    if (flags & SecretNotSaved)
        return "ask";
    return "save";
}

bool shown(const Field& field, const std::vector<Field>& fields, const Values& values)
{
    if (field.showWhen.empty())
        return true;
    const size_t equals = field.showWhen.find('=');
    const std::string key = field.showWhen.substr(0, equals);
    const std::string accepted = equals == std::string::npos ? std::string() : field.showWhen.substr(equals + 1);
    std::string value;
    for (const Field& other : fields) {
        if (other.key != key)
            continue;
        const auto raw = values.find(key);
        if (other.type == Type::Bool)
            value = (raw != values.end() && toBool(raw->second)) != other.inverted ? other.trueValue : other.falseValue;
        else if (raw == values.end())
            value = other.defaultValue;
        else
            value = raw->second;
        break;
    }
    // "key=" accepts the empty value, which split() would drop.
    size_t start = 0;
    while (true) {
        const size_t bar = accepted.find('|', start);
        if (accepted.substr(start, bar == std::string::npos ? std::string::npos : bar - start) == value)
            return true;
        if (bar == std::string::npos)
            return false;
        start = bar + 1;
    }
}

std::vector<std::string> kinds()
{
    return {"openvpn", "openconnect", "vpnc", "l2tp", "pptp", "sstp", "strongswan", "libreswan",
            "fortisslvpn", "ssh", "iodine"};
}

std::vector<Field> fields(const std::string& kind)
{
    const auto found = tables().find(kind);
    return found == tables().end() ? std::vector<Field>() : found->second.fields;
}

std::vector<std::string> importPatterns(const std::string& kind)
{
    if (kind == "openvpn")
        return {"*.ovpn", "*.conf"};
    if (kind == "vpnc")
        return {"*.pcf"};
    if (kind == "l2tp" || kind == "openconnect" || kind == "libreswan")
        return {"*.conf"};
    return {};
}

bool exportable(const std::string& kind)
{
    return !importPatterns(kind).empty();
}

std::vector<FormField> formFields(const std::string& kind, const std::map<std::string, std::string>& data)
{
    const auto valueOf = [&data](const std::string& key) {
        const auto found = data.find(key);
        return found == data.end() ? std::string() : found->second;
    };

    std::vector<FormField> list;
    for (const Field& field : fields(kind)) {
        FormField item;
        static_cast<Field&>(item) = field;
        const std::string stored = valueOf(field.key);
        switch (field.type) {
        case Type::Bool: {
            bool on;
            if (field.exclusiveKeys)
                on = false;
            else if (stored.empty())
                on = field.defaultValue == field.trueValue;
            else
                on = stored == field.trueValue;
            item.checked = on != field.inverted;
            break;
        }
        case Type::Choice: {
            std::string value = stored.empty() ? field.defaultValue : stored;
            if (field.exclusiveKeys) {
                value.clear();
                for (const Choice& choice : field.choices) {
                    if (!choice.value.empty() && valueOf(choice.value) == "yes")
                        value = choice.value;
                }
            }
            const bool known = std::any_of(field.choices.begin(), field.choices.end(),
                                           [&value](const Choice& choice) { return choice.value == value; });
            // A value from another editor stays as it is, shown as itself.
            if (!known)
                item.choices.push_back({value, value});
            item.value = value;
            break;
        }
        case Type::Password: {
            const std::string flags = valueOf(field.key + "-flags");
            item.storage = flags.empty() ? uint32_t(SecretAgentOwned) : uint32_t(std::strtoul(flags.c_str(), nullptr, 10));
            break;
        }
        default:
            item.value = stored;
            break;
        }
        list.push_back(std::move(item));
    }
    return list;
}

bool apply(const std::string& kind, const Values& values, const std::map<std::string, uint32_t>& storage,
           const std::map<std::string, std::string>& secrets, bool shared, std::map<std::string, std::string>& data,
           std::map<std::string, std::string>& vpnSecrets, std::string* error)
{
    const std::vector<Field> fields = schema::fields(kind);
    if (fields.empty()) {
        if (error)
            *error = "This kind of VPN can't be set up by hand.";
        return false;
    }

    for (const Field& field : fields) {
        // A hidden field keeps what the profile has, as the editors leave it alone too.
        if (!shown(field, fields, values))
            continue;
        const auto raw = values.find(field.key);
        const std::string value = raw == values.end() ? std::string() : raw->second;
        const std::string trimmed = text::trimmed(value);

        switch (field.type) {
        case Type::Bool: {
            const bool on = toBool(value) != field.inverted;
            const bool written =
                field.defaultValue.empty() ? on : (on ? field.trueValue : field.falseValue) != field.defaultValue;
            if (on)
                data[field.key] = field.trueValue;
            else if (!field.falseValue.empty() && written)
                data[field.key] = field.falseValue;
            else
                data.erase(field.key);
            for (const auto& [key, also] : field.alsoSet) {
                if (on)
                    data[key] = also;
                else
                    data.erase(key);
            }
            break;
        }
        case Type::Choice:
            if (field.exclusiveKeys) {
                for (const Choice& choice : field.choices) {
                    if (!choice.value.empty())
                        data.erase(choice.value);
                }
                if (!trimmed.empty())
                    data[trimmed] = "yes";
            } else if (trimmed.empty() || trimmed == field.defaultValue) {
                data.erase(field.key);
            } else {
                data[field.key] = trimmed;
            }
            break;
        case Type::Password: {
            const auto chosen = storage.find(field.key);
            const uint32_t flags = chosen != storage.end() ? chosen->second
                                                           : uint32_t(shared ? SecretSystemOwned : SecretAgentOwned);
            data[field.key + "-flags"] = std::to_string(flags);
            if (!field.legacyTypeKey.empty())
                data[field.legacyTypeKey] = legacyType(flags);
            const auto typed = secrets.find(field.key);
            if (flags & (SecretNotSaved | SecretNotRequired))
                vpnSecrets.erase(field.key);
            else if (typed != secrets.end() && !typed->second.empty())
                vpnSecrets[field.key] = typed->second;
            break;
        }
        case Type::Number:
            if (!trimmed.empty()) {
                char* end = nullptr;
                const long number = std::strtol(trimmed.c_str(), &end, 10);
                const bool ok = end && *end == '\0' && number >= INT32_MIN && number <= INT32_MAX;
                if (!ok || (field.maximum > field.minimum && (number < field.minimum || number > field.maximum))) {
                    if (error)
                        *error = field.label + " must be a number from " + std::to_string(field.minimum) + " to "
                                 + std::to_string(field.maximum) + ".";
                    return false;
                }
            }
            [[fallthrough]];
        default:
            if (trimmed.empty() && field.required) {
                if (error)
                    *error = "Enter the " + text::lower(field.label) + ".";
                return false;
            }
            if (trimmed.empty())
                data.erase(field.key);
            else
                data[field.key] = trimmed;
            break;
        }
    }
    return true;
}

} // namespace cutefish::networkmanager::vpn::schema
