// revisa token new NOMBRE | list | revoke NOMBRE: the client tokens of the local server.

#include "cli/commands.hpp"
#include "cli/messages.hpp"
#include "core/tokens/tokens.hpp"

namespace revisa::cli {

int run_token(const Args& args, const Host& host) {
    const std::string path = tokens::tokens_path();
    if (path.empty()) {
        host.err(msg(Msg::TokensNoPath));
        return kExitError;
    }
    const std::string& action = args.texts.at(0);

    if (action == "list") {
        const tokens::LoadResult loaded = tokens::load(path);
        if (!loaded.entries) {
            host.err(msg(Msg::TokensFile, {loaded.error}));
            return kExitError;
        }
        if (loaded.entries->empty()) host.out(msg(Msg::TokenNone));
        for (const tokens::Entry& e : *loaded.entries) host.out(msg(Msg::TokenListLine, {e.name, e.created}));
        return kExitUnchanged;
    }

    const std::string& name = args.texts.at(1);
    const bool adding = action == "new";
    const tokens::Change c = adding ? tokens::add(path, name) : tokens::revoke(path, name);
    switch (c.status) {
        case tokens::Status::Ok:
            if (adding) {
                host.out(c.token + "\n");
                host.err(msg(Msg::TokenCreated, {name}));
            } else {
                host.err(msg(Msg::TokenRevoked, {name}));
            }
            return kExitUnchanged;
        case tokens::Status::InvalidName: host.err(msg(Msg::TokenBadName)); break;
        case tokens::Status::Duplicate: host.err(msg(Msg::TokenDuplicate, {name})); break;
        case tokens::Status::NotFound: host.err(msg(Msg::TokenNotFound, {name})); break;
        case tokens::Status::FileError: host.err(msg(Msg::TokensFile, {c.error})); break;
    }
    return kExitError;
}

}  // namespace revisa::cli
