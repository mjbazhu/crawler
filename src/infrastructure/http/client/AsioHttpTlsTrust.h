#pragma once

#include <boost/asio/ssl/context.hpp>
#include <boost/system/error_code.hpp>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

// 在线程启动前装载默认信任；Windows 追加系统 ROOT，不改变证书与主机名校验。
boost::system::error_code loadDefaultCertificateTrust(boost::asio::ssl::context &context);

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
