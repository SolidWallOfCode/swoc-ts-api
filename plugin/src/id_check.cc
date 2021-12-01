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

#include <swoc/TextView.h>

using swoc::TextView;
using namespace swoc::literals;
/* ------------------------------------------------------------------------------------ */
namespace
{

/// Plugin configuration data.
struct Config {
  using self_type = Config;
public:
  using Handle = std::shared_ptr<self_type>;
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
  auto errata         = cfg->load_cli_args(cfg, G._args, 1);
  if (!errata.is_ok()) {
    std::string err_str;
    swoc::bwprint(err_str, "{}: Failed to reload configuration.\n{}", Config::PLUGIN_NAME, errata);
    TSError("%s", err_str.c_str());
  } else {
    std::unique_lock lock(Plugin_Config_Mutex);
    Plugin_Config = cfg;
  }
  Plugin_Reloading = false;
}

int
CB_TxnBoxMsg(TSCont, TSEvent, void *data)
{
  static constexpr TextView TAG{"txn_box."};
  static constexpr TextView RELOAD("reload");
  auto msg = static_cast<TSPluginMsg *>(data);
  if (TextView tag{msg->tag, strlen(msg->tag)}; tag.starts_with_nocase(TAG)) {
    tag.remove_prefix(TAG.size());
    if (0 == strcasecmp(tag, RELOAD)) {
      bool expected = false;
      if (Plugin_Reloading.compare_exchange_strong(expected, true)) {
        ts::PerformAsTask(&Task_ConfigReload);
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
  TSDebug("txn_box", "Core shut down");
  std::unique_lock lock(Plugin_Config_Mutex);
  Plugin_Config.reset();
  return TS_SUCCESS;
}

Errata
Init()
{
  TSPluginRegistrationInfo info{Config::PLUGIN_TAG.data(), "Verizon Media", "solidwallofcode@verizonmedia.com"};

  Plugin_Config = std::make_shared<Config>();
  auto errata   = Plugin_Config->load_cli_args(Plugin_Config, G._args, 1);
  if (!errata.is_ok()) {
    return errata;
  }
  auto delta = std::chrono::system_clock::now() - t0;
  std::string text;
  TSDebug(Config::PLUGIN_TAG.data(), "%s",
          swoc::bwprint(text, "{} files loaded in {} ms.", Plugin_Config->file_count(),
                        std::chrono::duration_cast<std::chrono::milliseconds>(delta).count())
            .c_str());

  if (TSPluginRegister(&info) == TS_SUCCESS) {
    TSCont cont{TSContCreate(CB_Txn_Start, nullptr)};
    TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, cont);
    G.reserve_txn_arg();
  } else {
    errata.note(R"({}: plugin registration failed.)", Config::PLUGIN_TAG);
    return errata;
  }
  return {};
}

} // namespace

void
TSPluginInit(int argc, char const *argv[])
{
  std::string err_str;
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, TSContCreate(&CB_TxnBoxMsg, nullptr));
  TSLifecycleHookAdd(TS_LIFECYCLE_SHUTDOWN_HOOK, TSContCreate(&CB_TxnBoxShutdown, nullptr));
  TSPluginDSOReloadEnable(false);
};
/* ------------------------------------------------------------------------ */
