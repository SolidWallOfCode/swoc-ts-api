/** @file
   Base plugin logic.

 * Copyright 2021 LinkedIn
 * SPDX-License-Identifier: Apache-2.0
*/

#include <string>
#include <map>
#include <numeric>
#include <atomic>
#include <shared_mutex>

#include "ts_util.h"
#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/BufferWriter.h>

using swoc::TextView;
using swoc::MemSpan;
using swoc::Errata;
using namespace swoc::literals;
/* ------------------------------------------------------------------------------------ */
namespace
{

/// Plugin configuration data.
struct Config {
  using self_type = Config;
public:
  using Handle = std::shared_ptr<self_type>;

  static inline constexpr TextView PLUGIN_NAME { "id_check" };
  static inline constexpr TextView PLUGIN_MSG_PREFIX { "id_check." };

  Errata load(swoc::file::path const& file);
  bool contains(uint64_t id);

protected:
  /// Sorted list of IDs.
  std::vector<uint64_t> _data;
};

Config::Handle Plugin_Config;
std::shared_mutex Plugin_Config_Mutex; // safe updating of the shared ptr.
std::atomic<bool> Plugin_Reloading = false;

// Get a shared pointer to the configuration safely against updates.
Config::Handle
scoped_plugin_config()
{
  std::shared_lock lock(Plugin_Config_Mutex);
  return Plugin_Config;
}

/* ------------------------------------------------------------------------------------ */
void
Task_ConfigReload()
{
  std::shared_ptr cfg = std::make_shared<Config>();
  swoc::Errata errata;
  if (!errata.is_ok()) {
    std::string err_str;
    swoc::bwprint(err_str, "{}: Failed to load configuration.\n{}", Config::PLUGIN_NAME, errata);
    TSError("%s", err_str.c_str());
  } else {
    std::unique_lock lock(Plugin_Config_Mutex);
    Plugin_Config = cfg;
  }
  Plugin_Reloading = false;
}

int
CB_Msg(TSCont, TSEvent, void *data)
{
  static constexpr TextView RELOAD_TAG("reload");

  auto msg = static_cast<TSPluginMsg *>(data);
  if (TextView tag{msg->tag, strlen(msg->tag)}; tag.starts_with_nocase(Config::PLUGIN_MSG_PREFIX) {
    tag.remove_prefix(Config::PLUGIN_MSG_PREFIX.size());
    if (0 == strcasecmp(tag, RELOAD_TAG)) {
      bool expected = false;
      if (Plugin_Reloading.compare_exchange_strong(expected, true)) {
        (void) ts::PerformAsTask(&Task_ConfigReload);
      } else {
        std::string err_str;
        swoc::bwprint(err_str, "{}: Reload requested while previous reload still active", Config::PLUGIN_NAME);
        TSError("%s", err_str.c_str());
      }
    }
  }
  return TS_SUCCESS;
}

int
CB_Shutdown(TSCont, TSEvent, void *)
{
  TSDebug(Config::PLUGIN_NAME.data(), "Core shut down");
  // Clean up the config.
  std::unique_lock lock(Plugin_Config_Mutex);
  Plugin_Config.reset();
  return TS_SUCCESS;
}

Errata
Config::load(swoc::file::path const& file) {
  static auto is_delim = [](char c) { return isspace(c) || ',' == c; };

  std::error_code ec;
  auto content = swoc::file::load(file, ec);
  if (ec) {
    return { ec, ts::S_ERROR, "Failed to open datapack {} - {}", file, ec};
  }

  TextView text { content };
  TextView token;
  while (!(token = text.ltrim_if(is_delim).take_prefix_if(is_delim)).empty()) {
    TextView parsed;
    auto n = swoc::svtou(token, &parsed);
    if (parsed.size() != token.size()) {
    } else {
      _data.push_back(n);
    }
  }
  std::sort(_data.begin(), _data.end(), std::less<decltype(_data)::value_type>());
  return {};
}

bool Config::contains(uint64_t id) {
  auto left = _data.begin();
  auto right = _data.end();
  while (left < right) {
    auto spot = left + (right - left)/2; // round down because right is past end.

  }
}

Errata
Init(MemSpan<char const*> argv)
{
  TSPluginRegistrationInfo info{Config::PLUGIN_NAME.data(), "LinkedIn", "traffic@linkedin.com"};

  Plugin_Config = std::make_shared<Config>();
  Errata errata;
  if (!errata.is_ok()) {
    return errata;
  }

  std::string path;
  TSDebug(Config::PLUGIN_NAME.data(), "Configuration loaded");

  static constexpr TextView KEY_PATH    = "path";

  for (unsigned idx = 0; idx < argv.count(); ++idx) {
    TextView arg{argv[idx], TextView::npos};
    if (arg.empty()) {
      continue;
    }
    if (arg.front() == '-') {
      arg.ltrim('-');
      if (arg.empty()) {
        return Errata(ts::S_ERROR, "Arg {} has an option prefix but no name.", idx);
      }

      TextView value;
      if (auto prefix = arg.prefix_at('='); !prefix.empty()) {
        value = arg.substr(prefix.size() + 1);
        arg   = prefix;
      } else if (++idx >= argv.count()) {
        return Errata(ts::S_ERROR, "Arg {} is an option '{}' that requires a value but none was found.", idx, arg);
      } else {
        value = std::string_view{argv[idx]};
      }

      if (arg.starts_with_nocase(KEY_PATH)) {
        path = value;
      } else {
        return Errata(ts::S_ERROR, "Arg {} is an unrecognized option '{}'.", idx, arg);
      }
      continue;
    }  return {};
}

} // namespace

void
TSPluginInit(int argc, char const *argv[])
{
  std::string err_str;

  Init(MemSpan{ argv + 1, size_t(argc) - 1 });



  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, TSContCreate(&CB_Msg, nullptr));
  TSLifecycleHookAdd(TS_LIFECYCLE_SHUTDOWN_HOOK, TSContCreate(&CB_Shutdown, nullptr));
  TSPluginDSOReloadEnable(false);
};
/* ------------------------------------------------------------------------ */
