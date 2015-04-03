#include "cocaine/detail/service/node.v2/app.hpp"

#include "cocaine/api/isolate.hpp"

#include "cocaine/context.hpp"
#include "cocaine/rpc/actor.hpp"

#include "cocaine/detail/service/node/event.hpp"
#include "cocaine/detail/service/node/manifest.hpp"
#include "cocaine/detail/service/node/profile.hpp"

#include "cocaine/detail/service/node.v2/actor.hpp"
#include "cocaine/detail/service/node.v2/drone.hpp"
#include "cocaine/detail/service/node.v2/slot.hpp"

#include "cocaine/idl/node.hpp"
#include "cocaine/idl/rpc.hpp"

#include <tuple>

using namespace cocaine;
using namespace cocaine::service::v2;

using namespace blackhole;

namespace ph = std::placeholders;

template<class T> class deduce;

// From client to worker.
class streaming_dispatch_t:
    public dispatch<io::event_traits<io::app::enqueue>::dispatch_type>
{
public:
    explicit streaming_dispatch_t(const std::string& name):
        dispatch<io::event_traits<io::app::enqueue>::dispatch_type>(name)
    {
        typedef io::protocol<io::event_traits<io::app::enqueue>::dispatch_type>::scope protocol;

        on<protocol::chunk>(std::bind(&streaming_dispatch_t::write, this, ph::_1));
        on<protocol::error>(std::bind(&streaming_dispatch_t::error, this, ph::_1, ph::_2));
        on<protocol::choke>(std::bind(&streaming_dispatch_t::close, this));
    }

private:
    void
    write(const std::string&) {
    }

    void
    error(int, const std::string&) {
    }

    void
    close() {
    }
};

/// Drone - single process representation.
///  - spawns using isolate.
///  - captures inputs/outputs.
///  - can get statistics.
///  - lives until process lives and vise versa.
///
/// Overlord - multiple drone communicator and multiplexor.
///  - dispatches control messages.
///
/// Overseer - drone spawner.

class handshake_dispatch : public dispatch<io::rpc_tag> {
    std::shared_ptr<session_t> session;

public:
    handshake_dispatch(std::shared_ptr<session_t> session) :
        dispatch<io::rpc_tag>("[app]/handshake"),
        session(session)
    {
        on<io::rpc::handshake>([](std::string){
        });
    }
};

class app_dispatch_t;
class cocaine::overlord_t:
    public dispatch<io::rpc_tag>
{
    std::unordered_map<std::string, std::shared_ptr<session_t>> sessions;

public:
    overlord_t() :
        dispatch<io::rpc_tag>("[app]/overlord")
    {}

    void
    on_session(std::shared_ptr<session_t> session) {
        session->inject(std::make_shared<handshake_dispatch>(session));
    }
};

/// App dispatch, manages incoming enqueue requests. Adds them to the queue.
class app_dispatch_t:
    public dispatch<io::app_tag>
{
    typedef io::streaming_slot<io::app::enqueue> slot_type;

    std::unique_ptr<logging::log_t> log;

public:
    app_dispatch_t(context_t& context, const engine::manifest_t& manifest) :
        dispatch<io::app_tag>(manifest.name),
        log(context.log(cocaine::format("app/%s", manifest.name)))
    {
        on<io::app::enqueue>(std::make_shared<slot_type>(
            std::bind(&app_dispatch_t::on_enqueue, this, ph::_1, ph::_2, ph::_3)
        ));
    }

    ~app_dispatch_t() {}

private:
    std::shared_ptr<const slot_type::dispatch_type>
    on_enqueue(slot_type::upstream_type&, const std::string& event, const std::string& tag) {
        if(tag.empty()) {
            COCAINE_LOG_DEBUG(log, "processing enqueue '%s' event", event);
            // Create message queue and cache `invoke` event immediately.
            // Create dispatch and pass `queue` there. This will be user -> worker channel.
            // Get client from the pool (by magic or some statistics). Create if necessary and inject session into message queue.
            // Invoke `client.invoke(dispatch, args...) -> stream`. This will be invoke + user -> worker channel.
            return std::make_shared<const streaming_dispatch_t>(name());
        } else {
            COCAINE_LOG_DEBUG(log, "processing enqueue '%s' event with tag '%s'", event, tag);
            // TODO: Complete!
            throw cocaine::error_t("on_enqueue: not implemented yet");
        }
    }
};

/// Represents a single application. Starts TCP and UNIX servers (the second inside the first).
app_t::app_t(context_t& context, const std::string& manifest, const std::string& profile) :
    context(context),
    manifest(new engine::manifest_t(context, manifest)),
    profile(new engine::profile_t(context, profile)),
    loop(std::make_shared<asio::io_service>())
{
    auto isolate = context.get<api::isolate_t>(
        this->profile->isolate.type,
        context,
        this->manifest->name,
        this->profile->isolate.args
    );

    // TODO: Start the service immediately, but set its state to `spooling` or somethinh else.
    // While in this state it can serve requests, but always return `invalid state` error.
    if(this->manifest->source() != cached<dynamic_t>::sources::cache) {
        isolate->spool();
    }

    start();
}

app_t::~app_t() {
    drone.reset();

    // TODO: Anounce all opened sessions to be closed (and sockets).
    engine->terminate();
    context.remove(manifest->name);
}

void app_t::start() {
    context.insert(manifest->name, std::make_unique<actor_t>(
        context,
        loop,
        std::make_unique<app_dispatch_t>(context, *manifest))
    );

    // Create unix actor and bind to {name}.sock. Owns: 1:1.
    auto overlord = std::make_unique<overlord_t>();
    engine.reset(new unix_actor_t(
        context,
        manifest->endpoint,
        std::bind(&overlord_t::on_session, overlord.get(), ph::_1),
        loop,
        std::move(overlord)
    ));
    engine->run();

    // TODO: Temporary spawn 1 slave. Remove later.
    drone_data d(*manifest, *profile, [this](const std::string& output){
        COCAINE_LOG_DEBUG(log, "output: %s", output);
    });
    drone = drone_t::make(context, std::move(d), loop);
}
