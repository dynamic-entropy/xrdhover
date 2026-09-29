#include "xrdhover/error_classifier.hh"

#include <cctype>
#include <string>

namespace xrdhover {
namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

const char* ErrorClassName(ErrorClass c) {
    switch (c) {
        case ErrorClass::None:
            return "none";
        case ErrorClass::Auth:
            return "auth";
        case ErrorClass::Timeout:
            return "timeout";
        case ErrorClass::Connection:
            return "connection";
        case ErrorClass::ServerError:
            return "server_error";
        case ErrorClass::NotFound:
            return "not_found";
        case ErrorClass::ClientError:
            return "client_error";
        case ErrorClass::RedirectLoop:
            return "redirect_loop";
        case ErrorClass::Trust:
            return "trust";
        case ErrorClass::Expired:
            return "expired";
        case ErrorClass::NoReplicas:
            return "no_replicas";
        case ErrorClass::Unknown:
            return "unknown";
    }
    return "unknown";
}

ErrorClass ClassifyXRootDError(int status_code, int err_code, const std::string& message) {
    const std::string msg = Lower(message);

    // Auth
    if (err_code == 3010 || msg.find("not authorized") != std::string::npos ||
        msg.find("authentication") != std::string::npos || msg.find("permission denied") != std::string::npos ||
        msg.find("eacces") != std::string::npos || msg.find("gss error") != std::string::npos) {
        return ErrorClass::Auth;
    }

    // Not found
    if (err_code == 3001 || msg.find("not found") != std::string::npos ||
        msg.find("no such file") != std::string::npos) {
        return ErrorClass::NotFound;
    }

    // Redirect exhaustion / loops (AAA often surfaces "Redirect limit has been reached")
    if (msg.find("redirect limit") != std::string::npos || msg.find("redirect loop") != std::string::npos ||
        msg.find("too many redirects") != std::string::npos || msg.find("redirectlimit") != std::string::npos) {
        return ErrorClass::RedirectLoop;
    }

    // XrdCl says "Operation expired" (errOperationExpired = 206). That is a
    // client-side wait expiry, not the word "timeout", and errNo is often 0.
    if (status_code == 206 || msg.find("operation expired") != std::string::npos) {
        return ErrorClass::Expired;
    }

    // Timeouts
    if (status_code == 103 || msg.find("timeout") != std::string::npos ||
        msg.find("timed out") != std::string::npos || err_code == 110 /* ETIMEDOUT */) {
        return ErrorClass::Timeout;
    }

    // CA / certificate verification (before broad TLS/socket Connection match)
    if (msg.find("certificate verify") != std::string::npos ||
        msg.find("unable to get local issuer") != std::string::npos ||
        msg.find("unknown ca") != std::string::npos ||
        msg.find("self signed certificate") != std::string::npos ||
        msg.find("certificate unknown") != std::string::npos ||
        msg.find("sslv3 alert certificate") != std::string::npos ||
        msg.find("x509_v_err") != std::string::npos) {
        return ErrorClass::Trust;
    }

    // Transport / TLS / socket (FNAL handshakes often look like this)
    if (msg.find("tls") != std::string::npos || msg.find("error_ssl") != std::string::npos ||
        msg.find("ssl") != std::string::npos || msg.find("handshake") != std::string::npos ||
        msg.find("connection") != std::string::npos || msg.find("connect") != std::string::npos ||
        msg.find("socket") != std::string::npos || msg.find("resource temporarily unavailable") != std::string::npos ||
        msg.find("network is unreachable") != std::string::npos || err_code == 111 /* ECONNREFUSED */ ||
        err_code == 104 /* ECONNRESET */ || err_code == 11 /* EAGAIN */) {
        return ErrorClass::Connection;
    }

    // Remaining XRootD server-ish codes (kXR_* in 3000–3999).
    if (err_code >= 3000 && err_code < 4000) {
        if (err_code == 3005 /* kXR_InvalidRequest */ || err_code == 3002 /* kXR_NotFile */)
            return ErrorClass::ClientError;
        return ErrorClass::ServerError;
    }

    if (msg.find("server") != std::string::npos || msg.find("srverr") != std::string::npos) {
        return ErrorClass::ServerError;
    }

    // status_code is XrdCl::Status::code. The message heuristics above miss
    // several of these when errNo is 0.
    switch (status_code) {
        case 16:  // errNoMoreReplicas
            return ErrorClass::NoReplicas;
        case 101:  // errInvalidAddr
        case 102:  // errSocketError
        case 104:  // errSocketDisconnected
        case 105:  // errPollerError
        case 106:  // errSocketOptError
        case 107:  // errStreamDisconnect
        case 108:  // errConnectionError
        case 109:  // errInvalidSession
        case 110:  // errTlsError
        case 202:  // errHandShakeFailed
            return ErrorClass::Connection;
        case 203:  // errLoginFailed
        case 204:  // errAuthFailed
            return ErrorClass::Auth;
        case 304:  // errNotFound
            return ErrorClass::NotFound;
        case 306:  // errRedirectLimit
            return ErrorClass::RedirectLoop;
        case 303:  // errInvalidResponse
        case 307:  // errCorruptedHeader
        case 400:  // errErrorResponse
            return ErrorClass::ServerError;
        default:
            break;
    }
    if (msg.find("no more replicas") != std::string::npos ||
        msg.find("no servers are available") != std::string::npos) {
        return ErrorClass::NoReplicas;
    }

    if (!msg.empty() || err_code != 0 || status_code != 0) return ErrorClass::Unknown;
    return ErrorClass::None;
}

}  // namespace xrdhover
