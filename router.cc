#include "router.hh"

#include <iostream>
#include <limits>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";
  
  Route new_route = { route_prefix,prefix_length,next_hop,interface_num};
  routing_table.push_back(new_route);             
}
void Router::route() {
    for (AsyncNetworkInterface& interf : interfaces_){
  // create the new datagram with whatever is received on the interface
    optional<InternetDatagram> packet = interf.maybe_receive();

        if(!packet.has_value()){continue;}
        IPv4Datagram datagram = {packet->header, packet->payload};
        IPv4Header datagram_header = packet->header;
        //  this is our best route
        std::vector<Route>::iterator best_rte = {};
        //  boolean for whether we found the best route or not
        bool found = false;
        uint32_t longest_prefix = 0;
        // loop over the routing table
        for (const Route& curr: routing_table){
            uint8_t diff = 32 - curr.prefix_length;
            // if our prefix length is 0, we automatically add it to our list of good routes since it is a complete match
            if(curr.prefix_length == 0){ diff= 0; good_routes.push_back(curr);}
            // Now we perform right bit shifts on the dest & route addresses
            const uint32_t shifted_dest_addr = datagram_header.dst >> diff;
            const uint32_t shifted_route_addr = curr.route_prefix >> diff;
            // if our prefixes match, we add it to our list of good routes
            if(shifted_dest_addr == shifted_route_addr){
                good_routes.push_back(curr);
            }
        }
        // now we go over the list of good routes and find the best route (longest matching prefix)
        for(auto rte = good_routes.begin(); rte!=good_routes.end(); rte++){
            uint32_t common_prefix = rte->route_prefix & datagram.header.dst;
            if(common_prefix == rte->route_prefix && common_prefix > longest_prefix){
                longest_prefix = common_prefix;
                best_rte = rte;
                found = true;
            }
        }
        // if ttl is too low, drop packet
        if ( datagram.header.ttl <= 1 ) {continue;}
        // if we didn't find it then move on 
        if ( !found ) { continue;}
        //  decrement the ttl and compute the checksum
        datagram.header.ttl--;
        datagram.header.compute_checksum();

        // get the index of the sender and then use that interface
        size_t sending_index = best_rte->interface_num;
        AsyncNetworkInterface &sending_interf = interface( sending_index );
            // send the datagrams
        if ( best_rte->next_hop.has_value() ) {
                Address dest = best_rte->next_hop.value();
                sending_interf.send_datagram( datagram, dest );
            } 
        else {
                Address nxt_hop = Address::from_ipv4_numeric( datagram.header.dst );
                sending_interf.send_datagram( datagram, nxt_hop );
            }
            }
     }

