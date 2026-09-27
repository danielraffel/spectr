#include "spectr/spectr.hpp"
#include <pulp/format/clap_entry.hpp>
#if defined(SPECTR_SHARED_NATIVE_HOST_PROBE)
#include "spectr/experimental/shared_host_probe_entry.hpp"
namespace spectr::host_probe {
inline Processor* processor(const clap_plugin_t* plugin) {
    auto* adapter=static_cast<pulp::format::clap_adapter::PulpClapPlugin*>(plugin->plugin_data);
    return static_cast<Processor*>(adapter->processor.get());
}
inline bool start_processing(const clap_plugin_t* plugin) {
    auto* p=processor(plugin);
    if(!p||!p->start_processing_probe())return false;
    if(!pulp::format::clap_adapter::clap_start_processing(plugin)){
        p->stop_processing_probe();return false;
    }
    return true;
}
inline void stop_processing(const clap_plugin_t* plugin) {
    pulp::format::clap_adapter::clap_stop_processing(plugin);
    if(auto* p=processor(plugin))p->stop_processing_probe();
}
inline const clap_plugin_t* create_plugin(const clap_plugin_factory_t* factory,
                                         const clap_host_t* host,const char* id) {
    const auto* plugin=pulp::format::clap_generic::create_plugin(factory,host,id);
    if(!plugin)return nullptr;
    auto* adapter=static_cast<pulp::format::clap_adapter::PulpClapPlugin*>(plugin->plugin_data);
    adapter->plugin.start_processing=start_processing;
    adapter->plugin.stop_processing=stop_processing;
    return plugin;
}
inline const clap_plugin_factory_t factory={
    pulp::format::clap_generic::get_plugin_count,
    pulp::format::clap_generic::get_plugin_descriptor,create_plugin};
inline const void* get_factory(const char* id){
    return std::strcmp(id,CLAP_PLUGIN_FACTORY_ID)==0?&factory:nullptr;
}
struct Registrar {
    Registrar(){
        pulp::format::clap_generic::register_clap_record(create,false);
        pulp::format::register_plugin(create);
    }
};
inline Registrar registrar;
}
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry={
    CLAP_VERSION,pulp::format::clap_generic::entry_init,
    pulp::format::clap_generic::entry_deinit,spectr::host_probe::get_factory};
#else
PULP_CLAP_PLUGIN(spectr::create_spectr)
#endif
