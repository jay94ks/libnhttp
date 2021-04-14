#include "http_websock_ep.hpp"
#include "../http_context.hpp"
#include "../http_link.hpp"
#include "../../depends/sha1/sha1.hpp"
#include "../internals/drivers/http_websocket_driver.hpp"

namespace nhttp {
namespace server {

    /**
     * class websocket_tag.
     * internal use for identifying it is websocket session or not.
     */
    class http_websocket_tag {
    public:
        time_t handshaked_at;
    };

    bool http_websocket_ep::on_collect(std::shared_ptr<http_context> context) {
        const auto& method = context->request->get_target().get_method();

        if (method == http_method::GET) {
            const auto& headers = context->request->get_headers();

            auto connection = headers.get(http_header::CONNECTION);
            auto upgrade = headers.get(http_header::UPGRADE);

            return connection && upgrade && 
                !strnicmp(connection, "upgrade", 7) &&
                !strnicmp(upgrade, "websocket", 8);
        }

        return false;
    }

    bool http_websocket_ep::on_handle(std::shared_ptr<http_context> context) {
        http_websocket_tag* ws_tag = context->link->get_tag_ptr<http_websocket_tag>();
        const auto& method = context->request->get_target().get_method();

        if (ws_tag) {
            /* how could reached this branch...? */
            context->close(false);
            return true;
        }

        else {
            const auto& headers = context->request->get_headers();
            auto sec_key = headers.find_one(http_header::SEC_WEBSOCKET_KEY);
            auto websocket = std::make_shared<http_websocket>(context);

            /* if `Sec-Websocket-Key` header not set, fall back to next extension. */
            if (sec_key == headers.vec.end() || !on_connect(websocket)) { return false; }

            char base64[SHA1_BASE64_SIZE];
            static const char* MAGIC_STRING = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            sha1().add((sec_key->get_value() + MAGIC_STRING).c_str())
                  .finalize().print_base64(base64);

            /* grant to upgrade connection to web socket. */
            if (!context->response) /* 101 Switching Protocol. */
                 context->response = make_response(101); 
            else context->response->status.set(101);

            context->response->headers.set(http_header::UPGRADE, "websocket");
            context->response->headers.set(http_header::CONNECTION, "upgrade");
            context->response->headers.set(http_header::SEC_WEBSOCKET_ACCEPT, base64);
            
            /* then, remember handshake timestamp. */
            context->link->set_tag<http_websocket_tag>()
                   ->handshaked_at = context->request->get_timestamp();

            /* upgrade the link to websocket driver. */
            context->link->replace(std::make_shared<drivers::http_websocket_driver>(websocket));
            context->close(true);
            return true;
        }

        return false;
    }

}
}