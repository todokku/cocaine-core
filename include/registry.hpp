#ifndef YAPPI_REGISTRY_HPP
#define YAPPI_REGISTRY_HPP

#include "common.hpp"
#include "plugin.hpp"

namespace yappi { namespace core {

class registry_t {
    public:
        registry_t();
        ~registry_t();

        plugin::source_t* instantiate(const std::string& uri);

    private:
        // Used to instantiate plugin instances
        typedef std::map<const std::string, plugin::factory_fn_t> factory_map_t;
        factory_map_t m_factories;

        // Used to dlclose() all the plugins on shutdown
        std::vector<void*> m_plugins;
};

}}

#endif
