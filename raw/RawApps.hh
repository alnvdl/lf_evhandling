#ifndef __RAWAPPS_HH__
#define __RAWAPPS_HH__

#include <arpa/inet.h>

#include "of10.hh"
#include "Controller.hh"
#include <unistd.h>
#include <boost/lockfree/spsc_queue.hpp>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <pcap.h>
#include <sys/time.h>
#include <queue>

// #define RINGBUFFER
// #define COMPLETION
// #define SYNCQUEUE

#define SLEEP_DELAY 50000

#ifdef SYNCQUEUE
pthread_t syncqueue_capture_thread;
std::queue<struct pkt*> sync_queue;
pthread_mutex_t sync_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
void sync_queue_lock() {
    pthread_mutex_lock(&sync_queue_mutex);
}
void sync_queue_unlock() {
    pthread_mutex_unlock(&sync_queue_mutex);
}
int quitcap;
#endif

struct pkt {
    OFHandler* ofhandler;
    struct ofp_packet_in* ofpi;
};

pcap_t* pd;
pcap_dumper_t* pdumper;

#ifdef RINGBUFFER
pthread_t ringbuffer_capture_thread;
boost::lockfree::spsc_queue<struct pkt*, boost::lockfree::capacity<10000000>> print_queue;
int quitcap;
#endif

void log_packet(struct pkt* value) {
    struct pcap_pkthdr h;
    gettimeofday(&(h.ts), NULL);
    h.caplen = ntohs(value->ofpi->total_len);
    h.len = ntohs(value->ofpi->total_len);
    
    pcap_dump((u_char*) pdumper, &h, (u_char*) value->ofpi->data);
    
    value->ofhandler->free_data(value->ofpi);
    delete value;
}

#ifdef SYNCQUEUE
void* syncqueue_capture(void* arg) {
    quitcap = 0;
    
    struct sched_param params;
    params.sched_priority = sched_get_priority_max(SCHED_IDLE);
    if (pthread_setschedparam(pthread_self(), SCHED_IDLE, &params) != 0) {
        std::cout << "Error setting thread priority!" << std::endl;
        return NULL;
    }
    
    struct pkt* value;
    
    while (not quitcap) {
        sync_queue_lock();
        while (not sync_queue.empty()) {
            value = sync_queue.front();
            sync_queue.pop();
            log_packet(value);
        }
        sync_queue_unlock();
        // Sleep a while to avoid this thread eating up resources
	usleep(SLEEP_DELAY);
    }
    
    return NULL;
}
#endif

#ifdef RINGBUFFER
void* ringbuffer_capture(void* arg) {
    quitcap = 0;
    
    struct sched_param params;
    params.sched_priority = sched_get_priority_max(SCHED_IDLE);
    if (pthread_setschedparam(pthread_self(), SCHED_IDLE, &params) != 0) {
        std::cout << "Error setting thread priority!" << std::endl;
        return NULL;
    }
    struct pkt* value;
    
    while (not quitcap) {
        while (print_queue.pop(value)) {
            log_packet(value);
        }
	// Sleep a while to avoid this thread eating up resources
        usleep(SLEEP_DELAY);
    }
    
    return NULL;
}
#endif

void start_capture() {
    pd = pcap_open_dead(DLT_EN10MB, 65535);
    pdumper = pcap_dump_open(pd, "/tmp/test.cap");
    
    #ifdef RINGBUFFER
    pthread_create(&ringbuffer_capture_thread, NULL, ringbuffer_capture, NULL);
    #endif
    
    #ifdef SYNCQUEUE
    pthread_create(&syncqueue_capture_thread, NULL, syncqueue_capture, NULL);
    #endif
}

void stop_capture() {
    #ifdef RINGBUFFER
    quitcap = 1;
    pthread_join(ringbuffer_capture_thread, NULL);
    #endif

    pcap_close(pd);
    pcap_dump_close(pdumper);
}

class RawCBench : public Application {
    virtual void event_callback(ControllerEvent* ev) {
        if (ev->get_type() == EVENT_PACKET_IN) {
            struct ofp_flow_mod fm;
            memset((void*) &fm, 0, sizeof(struct ofp_flow_mod));
            fm.header.version = 0x01;
            fm.header.type = OFPT_FLOW_MOD;
            fm.header.length = htons(sizeof(struct ofp_flow_mod));
            fm.header.xid = 0;
            fm.command = OFPFC_ADD;
            ev->ofconn->send((uint8_t*) &fm, sizeof(struct ofp_flow_mod));
        }
    }
};

class RawLearningSwitch : public BaseLearningSwitch {
public:
    RawLearningSwitch() {
        #ifdef RINGBUFFER
            printf("Running with RINGBUFFER event handling\n");
        #elif COMPLETION
            printf("Running with COMPLETION event handling\n");        
        #elif SYNCQUEUE
            printf("Running with SYNCQUEUE event handling\n");
        #else
            printf("No event handling method configured! Exiting...\n");
            exit(1);
        #endif
    }
    
    virtual void event_callback(ControllerEvent* ev) {
        if (ev->get_type() == EVENT_PACKET_IN) {
            L2TABLE* l2table = 
                get_l2table(ev->ofconn);
            if (l2table == NULL) {
                return;
            }
            
            int conn_id = ev->ofconn->get_id();

            PacketInEvent* pi = static_cast<PacketInEvent*>(ev);
            struct ofp_packet_in* ofpi = (struct ofp_packet_in*) pi->data;

            uint64_t dst = 0, src = 0;
            
            memcpy( ((uint8_t*) &dst) + 2, ofpi->data, 6);
            memcpy( ((uint8_t*) &src) + 2, ofpi->data + 6, 6);
            
            uint16_t in_port = ntohs(ofpi->in_port);
            
            // Learn the source
            (*l2table)[src] = in_port;
            
            // Try to find the destination
            L2TABLE::iterator it = l2table->find(dst);
            if (it == l2table->end()) {
                send_packet_out(pi, OFPP_FLOOD);
                return;
            }
            
            bool logging = (conn_id == 2);
            
            // If we're not logging, we want to install a flow to learn
            if (not logging) {
                install_flow_mod(pi, ev->ofconn, ofpi->header.xid, src, dst, it->second);
            }
            // If we're logging, we just want to send the packet out and log
            else {
                send_packet_out(pi, (*l2table)[dst]);
                pi->keep = true;
                this->log(pi->ofhandler, ofpi);
            }
        }
        else {
            BaseLearningSwitch::event_callback(ev);
        }
    }

    void install_flow_mod(PacketInEvent* pi, OFConnection* ofconn, uint32_t xid, uint64_t src, uint64_t dst, uint16_t out_port) {
        struct ofp_packet_in* ofpi = (struct ofp_packet_in*) pi->data;

        uint16_t msg_len = 72 + 8;
        uint8_t data[msg_len];
        uint8_t* raw = (uint8_t*) &data;
        memset(raw, 0, msg_len);

        // Flow mod message
        struct ofp_flow_mod fm;
        fm.header.version = 0x01;
        fm.header.type = OFPT_FLOW_MOD;
        fm.header.length = htons(msg_len);
        fm.header.xid = xid;

        // Match
        memset(&fm.match, 0, sizeof(struct ofp_match));
        fm.match.wildcards = htonl(OFPFW_ALL & ~OFPFW_DL_SRC & ~OFPFW_DL_DST);
        memcpy((uint8_t*) &fm.match.dl_src, ((uint8_t*) &src) + 2, 6);
        memcpy((uint8_t*) &fm.match.dl_dst, ((uint8_t*) &dst) + 2, 6);

        // Flow mod body
        fm.cookie = 123;
        fm.command = OFPFC_ADD;
        fm.idle_timeout = htons(5);
        fm.hard_timeout = htons(10);
        fm.priority  = htons(100);
        fm.buffer_id = ofpi->buffer_id;
        fm.out_port = 0;
        fm.flags = 0;
        memcpy(raw, &fm, 72);

        // Output action
        struct ofp_action_output act;
        act.type = OFPAT_OUTPUT;
        act.len = htons(8);
        act.port = htons(out_port);
        act.max_len = 0;
        memcpy(raw + 72, &act, 8);

        ofconn->send(raw, msg_len);
    }

    void log(OFHandler* ofhandler, struct ofp_packet_in* ofpi) {
        struct pkt* p = new struct pkt;
        p->ofhandler = ofhandler;
        p->ofpi = ofpi;
        
        #ifdef RINGBUFFER
        while (!print_queue.push(p));
        #endif

        #ifdef SYNCQUEUE
        sync_queue_lock();
        sync_queue.push(p);
        sync_queue_unlock();
        #endif
                
        #ifdef COMPLETION
        log_packet(p);
        #endif
    }
    
    void send_packet_out(PacketInEvent* pi, uint16_t out_port) {
        struct ofp_packet_in* ofpi = (struct ofp_packet_in*) pi->data;

        uint16_t msg_len = 16 + 8 + ntohs(ofpi->total_len);
        uint8_t data[msg_len];
        uint8_t* raw = (uint8_t*) &data;
        memset(raw, 0, msg_len);

        // Packet out message
        struct ofp_packet_out po;
        po.header.version = 0x01;
        po.header.type = OFPT_PACKET_OUT;
        po.header.length = htons(msg_len);
        po.header.xid = ((uint32_t*) pi->data)[1];
        po.buffer_id = ofpi->buffer_id;
        po.in_port = ofpi->in_port;
        po.actions_len = htons(8);
        memcpy(raw, &po, 16);

        // Flood action
        struct ofp_action_output act;
        act.type = OFPAT_OUTPUT;
        act.len = htons(8);
        act.port = htons(out_port);
        act.max_len = 0;
        memcpy(raw + 16, &act, 8);

        // Packet content
        memcpy(raw + 16 + 8, pi->data, ntohs(ofpi->total_len));

        pi->ofconn->send(raw, msg_len);
    }
        
    void flood(PacketInEvent* pi) {
        struct ofp_packet_in* ofpi = (struct ofp_packet_in*) pi->data;

        uint16_t msg_len = 16 + 8 + ntohs(ofpi->total_len);
        uint8_t data[msg_len];
        uint8_t* raw = (uint8_t*) &data;
        memset(raw, 0, msg_len);

        // Packet out message
        struct ofp_packet_out po;
        po.header.version = 0x01;
        po.header.type = OFPT_PACKET_OUT;
        po.header.length = htons(msg_len);
        po.header.xid = ((uint32_t*) pi->data)[1];
        po.buffer_id = ofpi->buffer_id;
        po.in_port = ofpi->in_port;
        po.actions_len = htons(8);
        memcpy(raw, &po, 16);

        // Flood action
        struct ofp_action_output act;
        act.type = OFPAT_OUTPUT;
        act.len = htons(8);
        act.port = htons(OFPP_FLOOD);
        act.max_len = 0;
        memcpy(raw + 16, &act, 8);

        // Packet content
        memcpy(raw + 16 + 8, pi->data, ntohs(ofpi->total_len));

        pi->ofconn->send(raw, msg_len);
    }
};

#endif
