#include "AsioHttpTlsTrust.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {
namespace {

#ifdef _WIN32
// OpenSSL 默认路径通常不含 Windows 根证书；只读导入操作系统已信任的根。
bool addWindowsSystemRoots(boost::asio::ssl::context &context)
{
    const auto close_store = [](void *store) { CertCloseStore(store, 0); };
    const std::unique_ptr<void, decltype(close_store)> store(CertOpenSystemStoreW(0, L"ROOT"), close_store);
    if (!store) return false;
    auto *destination = SSL_CTX_get_cert_store(context.native_handle());
    if (!destination) return false;
    bool loaded = false;
    const CERT_CONTEXT *entry = nullptr;
    // CertEnumCertificatesInStore 在下一次枚举时释放前一个上下文。
    while ((entry = CertEnumCertificatesInStore(store.get(), entry)) != nullptr) {
        const unsigned char *encoded = entry->pbCertEncoded;
        const std::unique_ptr<X509, decltype(&X509_free)> certificate(d2i_X509(nullptr, &encoded, entry->cbCertEncoded), X509_free);
        if (certificate && X509_STORE_add_cert(destination, certificate.get()) == 1) loaded = true;
        // 重复根证书与无法解码的系统条目不污染后续 TLS 握手的错误队列。
        ERR_clear_error();
    }
    return loaded;
}
#endif

} // namespace

boost::system::error_code loadDefaultCertificateTrust(boost::asio::ssl::context &context)
{
    boost::system::error_code error;
    context.set_default_verify_paths(error);
#ifdef _WIN32
    if (addWindowsSystemRoots(context)) error.clear();
#endif
    return error;
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
