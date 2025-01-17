#include "network_interface.hh"
#include <map>
#include "arp_message.hh"
#include "ethernet_frame.hh"

using namespace std;

// ethernet_address: Ethernet (what ARP calls "hardware") address of the interface
// ip_address: IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( const EthernetAddress& ethernet_address, const Address& ip_address )
  : ethernet_address_( ethernet_address ), ip_address_( ip_address ), ready_to_send(), waiting(), IP_time(), arp_cache(), current_time()
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

// dgram: the IPv4 datagram to be sent
// next_hop: the IP address of the interface to send it to (typically a router or default gateway, but
// may also be another host if directly connected to the same network as the destination)

// Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) by using the
// Address::ipv4_numeric() method.


void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // This is our next_hop converted to uint32_t
  uint32_t next = next_hop.ipv4_numeric(); 
  
  
  if (!(arp_cache.find(next) == arp_cache.end())){ // If we find the IP in our cache table and know its MAC, create the frame and send
    EthernetAddress dest = arp_cache.at(next).first;

    // # create ethernet frame and put in the queue
    EthernetFrame frame;
    frame.header.type = EthernetHeader::TYPE_IPv4;
    frame.header.dst = dest;
    frame.header.src = this->ethernet_address_;
    frame.payload = serialize(dgram);
    ready_to_send.push(frame);
    
  }
  else{

    
    // We do not know the MAC Address so create the ARPMessage that's gonna be broadcasted
    ARPMessage arp_request = ARPMessage();
    arp_request.sender_ethernet_address = ethernet_address_;
    arp_request.sender_ip_address = ip_address_.ipv4_numeric();
    arp_request.opcode = ARPMessage::OPCODE_REQUEST;
    arp_request.target_ip_address = next;
    // Put the message into an Ethernet Frame and push onto the ready_to_send queue
    EthernetFrame arp_frame;
    arp_frame.header.type = EthernetHeader::TYPE_ARP;
    arp_frame.header.dst = ETHERNET_BROADCAST;
    arp_frame.header.src = this->ethernet_address_;
    arp_frame.payload = serialize(arp_request);
    if(!(waiting.find(arp_request.target_ip_address) == waiting.end())){
      if(!(waiting[arp_request.target_ip_address].empty())){
        ; // if we find the IP address in our waiting queue and the queue isn't empty, do nothing
      }
    }
    else{
        ready_to_send.push(arp_frame);
        IP_time[next] = current_time + 5000; // Give it 5 seconds of expiry time
    }
    EthernetFrame frame;
    frame.header.type = EthernetHeader::TYPE_IPv4;
    frame.header.src = this->ethernet_address_;
    frame.payload = serialize(dgram);
    std::queue<EthernetFrame> que;
    que.push(frame);
    waiting[next] = que;
  }
  }
  
// frame: the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame( const EthernetFrame& frame )
{ 

  optional<InternetDatagram> result = {};

  // Check if it is for us or not
  if (frame.header.dst == this->ethernet_address_ || frame.header.dst == ETHERNET_BROADCAST) {
    // Check if it's an IP Packet
    if (frame.header.type == EthernetHeader::TYPE_IPv4) {
      InternetDatagram parsed;
      if (parse(parsed, frame.payload)) {
        return parsed;
      }
    }
    // Check if it's an ARP
    else if (frame.header.type == EthernetHeader::TYPE_ARP) {
      ARPMessage parsed;
      // Parse it, and update our cache table
      if (parse(parsed, frame.payload)) {
        if(arp_cache.find(parsed.sender_ip_address) == arp_cache.end()){
          std::pair<EthernetAddress, size_t> new_cache_entry;
          new_cache_entry = std::make_pair(frame.header.src, current_time + 30000); //Cache it for 30 seconds
          arp_cache[parsed.sender_ip_address] = new_cache_entry;

        }
        // Check if it's a request for our IP, if it is, give a reply
        if (parsed.opcode == 1 && parsed.target_ip_address == this->ip_address_.ipv4_numeric()) {
          ARPMessage reply = ARPMessage();
          reply.sender_ethernet_address = this->ethernet_address_;
          reply.sender_ip_address = this->ip_address_.ipv4_numeric();
          reply.target_ethernet_address = parsed.sender_ethernet_address;
          reply.target_ip_address = parsed.sender_ip_address;
          reply.opcode = ARPMessage::OPCODE_REPLY;
          // Now we have our ARPMessage, so we just put it in an Ethernet Frame
          EthernetFrame reply_frame;
          reply_frame.header.type = EthernetHeader::TYPE_ARP;
          reply_frame.header.src = this->ethernet_address_;
          reply_frame.header.dst = parsed.sender_ethernet_address;
          reply_frame.payload = serialize(reply);
          ready_to_send.push(reply_frame);
          return {};
        }
        else if (parsed.opcode == 2){ //Check if it's a reply
          queue que = waiting[parsed.sender_ip_address];
          while(!(que.empty())){
            EthernetFrame fr = que.front();
            que.pop();
            fr.header.dst = parsed.sender_ethernet_address;
            ready_to_send.push(fr);
          }
        waiting.erase(parsed.sender_ip_address); //Get rid of the entry from waiting queue 
        IP_time.erase(parsed.sender_ip_address); // Get rid of the entry from the IP_time queue
        }
      }
    }
  } else {
    // It is not destined for us, discard
  }

  return result; 
}


// ms_since_last_tick: the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  // increment current_time to keep track of time 
  current_time += ms_since_last_tick;
  // now we do the 30 second check and remove older from the arp table
  for( auto it = arp_cache.begin(); it!= arp_cache.end();){
    size_t value = it->second.second;
    if (value <= current_time) {
        it = arp_cache.erase(it);
  }
  else{
    ++it;
  }
}

  // do the second part of tick

  for (auto it = IP_time.begin(); it != IP_time.end();) {
        if (it->second <= current_time) {
            // Get the key
            uint32_t key = it->first;

            // Delete the entry in IP_time
            it = IP_time.erase(it);

            // Delete the corresponding entry in waiting
            waiting.erase(key);
        } else {
            // Move to the next element in IP_time
            ++it;
        }
    }
}

optional<EthernetFrame> NetworkInterface::maybe_send()
{
  if(ready_to_send.size() != 0){
    EthernetFrame first_packet = ready_to_send.front();
    ready_to_send.pop();
    return first_packet;
  }
  return {};
}
