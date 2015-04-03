/*
    Copyright (c) 2011-2014 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2013-2014 Andrey Goryachev <andrey.goryachev@gmail.com>
    Copyright (c) 2011-2014 Other contributors as noted in the AUTHORS file.

    This file is part of Cocaine.

    Cocaine is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Cocaine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef COCAINE_LOCATOR_SERVICE_HPP
#define COCAINE_LOCATOR_SERVICE_HPP

#include "cocaine/api/cluster.hpp"
#include "cocaine/api/connect.hpp"
#include "cocaine/api/service.hpp"

#include "cocaine/detail/service/locator/routing.hpp"

#include "cocaine/idl/context.hpp"
#include "cocaine/idl/locator.hpp"

#include "cocaine/rpc/dispatch.hpp"

#include "cocaine/locked_ptr.hpp"

namespace cocaine {

class actor_t;

} // namespace cocaine

namespace cocaine { namespace service {

class locator_t;

namespace results {

typedef result_of<io::locator::resolve>::type resolve;
typedef result_of<io::locator::connect>::type connect;
typedef result_of<io::locator::cluster>::type cluster;

} // namespace results

class locator_cfg_t
{
public:
    locator_cfg_t(const std::string& name, const dynamic_t& args);

    std::string name;
    std::string uuid;

    // Restricted services.
    std::set<std::string> restricted;
};

class locator_t:
    public api::service_t,
    public api::cluster_t::interface,
    public dispatch<io::locator_tag>
{
    class remote_t;
    class expose_slot_t;

    typedef std::map<std::string, continuum_t> router_map_t;

    typedef std::map<std::string, api::client<io::locator_tag>> remote_map_t;
    typedef std::map<std::string, streamed<results::connect>>   stream_map_t;

    context_t& m_context;

    const std::unique_ptr<logging::log_t> m_log;
    const locator_cfg_t m_cfg;

    // Cluster interconnections.
    asio::io_service& m_asio;

    // Slot for context signals.
    std::shared_ptr<dispatch<io::context_tag>> m_signals;

    // Clustering components.
    std::unique_ptr<api::gateway_t> m_gateway;
    std::shared_ptr<api::cluster_t> m_cluster;

    // Used to resolve service names against routing groups, based on weights and other metrics.
    synchronized<router_map_t> m_routers;

    // Incoming sessions indexed by uuid. It is required to disambiguate between multiple different
    // instances on the same host, even if the instance was restarted on the same port.
    synchronized<remote_map_t> m_remotes;

    // Outgoing sessions indexed by uuid.
    synchronized<stream_map_t> m_streams;

    // Snapshot of the local service disposition. Synchronized with outgoing streams.
    std::map<std::string, results::resolve> m_snapshot;

public:
    locator_t(context_t& context, asio::io_service& asio, const std::string& name, const dynamic_t& args);

    virtual
   ~locator_t();

    // Service API

    virtual
    auto
    prototype() const -> const io::basic_dispatch_t&;

    // Cluster API

    virtual
    asio::io_service&
    asio();

    virtual
    void
    link_node(const std::string& uuid, const std::vector<asio::ip::tcp::endpoint>& endpoints);

    virtual
    void
    drop_node(const std::string& uuid);

    virtual
    std::string
    uuid() const;

private:
    auto
    on_resolve(const std::string& name, const std::string& seed) const -> results::resolve;

    auto
    on_connect(const std::string& uuid) -> streamed<results::connect>;

    void
    on_refresh(const std::vector<std::string>& groups);

    auto
    on_cluster() const -> results::cluster;

    // Context signals

    void
    on_service(const std::string& name, const results::resolve& meta, bool active);

    void
    on_context_shutdown();
};

}} // namespace cocaine::service

#endif
