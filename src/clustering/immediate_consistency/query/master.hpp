#ifndef CLUSTERING_IMMEDIATE_CONSISTENCY_QUERY_MASTER_HPP_
#define CLUSTERING_IMMEDIATE_CONSISTENCY_QUERY_MASTER_HPP_

#include "clustering/immediate_consistency/branch/broadcaster.hpp"
#include "clustering/immediate_consistency/query/metadata.hpp"

/* TODO: Right now we rely on the network to deliver messages from the parsers
to the `master_t` in the same order as the client sent them to the parsers. This
might not be a valid assumption. Consider using a FIFO enforcer or something.
Also, we need a way for the `master_t` to hand off the operations to the
`broadcaster_t` while preserving order. The current method of "call a blocking
function" doesn't cut it because we need to be able to run multiple reads and
writes simultaneously in different coroutines and also guarantee order between
them. */

template<class protocol_t>
class master_t {
public:
    master_t(
            mailbox_manager_t *mm,
            clone_ptr_t<directory_wview_t<std::map<master_id_t, master_business_card_t<protocol_t> > > > master_directory,
            typename protocol_t::region_t region,
            broadcaster_t<protocol_t> *b)
            THROWS_ONLY(interrupted_exc_t) :
        mailbox_manager(mm),
        broadcaster(b),
        read_mailbox(mailbox_manager, boost::bind(&master_t<protocol_t>::on_read,
                                                  this, _1, _2, _3, _4, auto_drainer_t::lock_t(&drainer))),
        write_mailbox(mailbox_manager, boost::bind(&master_t<protocol_t>::on_write,
                                                   this, _1, _2, _3, _4, auto_drainer_t::lock_t(&drainer))),
        registrar(mm, this),
        advertisement(master_directory, generate_uuid(),
                      master_business_card_t<protocol_t>(region, read_mailbox.get_address(), write_mailbox.get_address(), registrar.get_business_card())) {
    }

private:
    void on_read(typename protocol_t::read_t read, order_token_t otok, UNUSED fifo_enforcer_read_token_t token,
            mailbox_addr_t<void(boost::variant<typename protocol_t::read_response_t, std::string>)> response_address,
            auto_drainer_t::lock_t keepalive)
            THROWS_NOTHING
    {
        // TODO: Use the fifo enforcer token.

        keepalive.assert_is_holding(&drainer);
        try {
            typename protocol_t::read_response_t response = broadcaster->read(read, otok);
            send(mailbox_manager, response_address, boost::variant<typename protocol_t::read_response_t, std::string>(response));
        } catch (typename broadcaster_t<protocol_t>::mirror_lost_exc_t e) {
            send(mailbox_manager, response_address, boost::variant<typename protocol_t::read_response_t, std::string>(std::string(e.what())));
        } catch (typename broadcaster_t<protocol_t>::insufficient_mirrors_exc_t e) {
            send(mailbox_manager, response_address, boost::variant<typename protocol_t::read_response_t, std::string>(std::string(e.what())));
        }
    }

    void on_write(typename protocol_t::write_t write, order_token_t otok, UNUSED fifo_enforcer_write_token_t token,
            mailbox_addr_t<void(boost::variant<typename protocol_t::write_response_t, std::string>)> response_address,
            auto_drainer_t::lock_t keepalive)
            THROWS_NOTHING
    {
        // TODO: Use the fifo enforcer token.
        keepalive.assert_is_holding(&drainer);
        try {
            typename protocol_t::write_response_t response = broadcaster->write(write, otok);
            send(mailbox_manager, response_address, boost::variant<typename protocol_t::write_response_t, std::string>(response));
        } catch (typename broadcaster_t<protocol_t>::mirror_lost_exc_t e) {
            send(mailbox_manager, response_address, boost::variant<typename protocol_t::write_response_t, std::string>(std::string(e.what())));
        } catch (typename broadcaster_t<protocol_t>::insufficient_mirrors_exc_t e) {
            send(mailbox_manager, response_address, boost::variant<typename protocol_t::write_response_t, std::string>(std::string(e.what())));
        }
    }

    struct parser_lifetime_t {
        parser_lifetime_t(master_t *m, namespace_interface_business_card_t bc) {
            send(m->mailbox_manager, bc.ack_address);
        }
        ~parser_lifetime_t() {
            logDBG("parser_lifetime_t destructor has been called.\n");
        }
    };

    mailbox_manager_t *mailbox_manager;
    broadcaster_t<protocol_t> *broadcaster;
    auto_drainer_t drainer;

    typename master_business_card_t<protocol_t>::read_mailbox_t read_mailbox;
    typename master_business_card_t<protocol_t>::write_mailbox_t write_mailbox;

    registrar_t<namespace_interface_business_card_t, master_t *, parser_lifetime_t> registrar;

    resource_map_advertisement_t<master_id_t, master_business_card_t<protocol_t> > advertisement;
};

#endif /* CLUSTERING_IMMEDIATE_CONSISTENCY_QUERY_MASTER_HPP_ */
