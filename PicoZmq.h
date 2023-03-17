//
// Created by Cederic on 28/01/2023.
//

#ifndef PICO_EPAPER_CODE_ZMQHEADER_H
#define PICO_EPAPER_CODE_ZMQHEADER_H

#include <string>
#include <array>
#include <iostream>
#include <vector>
#include <iomanip>
#include <algorithm>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/util/queue.h"
#include "pico/cyw43_arch.h"

#define RECONNECT_DEFAULT_TIMEOUT (5 * 1000 * 1000)

#if DEBUG
    #define COUT(str) cout << str
#else
    #define COUT(str)
#endif

#if DEBUG_MESSAGE
    #define DUMP_MESSAGE_BYTES dump_bytes
    #define COUT_MESSAGE(str) cout << str
#else
    #define DUMP_MESSAGE_BYTES(A,B,C)
    #define COUT_MESSAGE(str)
#endif


using namespace std;

static const array<string, 4> names = {"PUB", "SUB", "PUSH", "PULL"};

class PicoZmq{
public:
    struct returnMessage{
        uint8_t topicID;
        vector<char> payload;
    };

    enum SocketTypes{
        PUB = 0,
        SUB = 1,
        PUSH = 2,
        PULL = 3,
    };
    PicoZmq(const string& remoteAddr, uint16_t remote_port, SocketTypes socket_type, uint8_t keepAliveTime = 0);
    virtual ~PicoZmq();

    [[nodiscard]] bool isConnected() const{return connected;}
    [[nodiscard]] bool gotMessage() {return !queue_is_empty(&receive_queue);}

    void setTopic(const string &newTopic){topic = newTopic;}
    err_t sendMessage(const string &message);
    err_t sendMessage(const vector<char> &message);
    uint8_t subscribe(const string &subTopic);

    returnMessage getMessage();

    void reconnect();

#if DEBUG || DEBUG_MESSAGE
    friend ostream& operator<<(ostream& out, PicoZmq::SocketTypes value);
    friend ostream& operator<<(ostream& out,const PicoZmq::SocketTypes *value);
#endif
private:
    static void tcp_client_err(void *arg, err_t err);
    static err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
    static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err);
    static err_t tcp_client_poll(void *arg, struct tcp_pcb *tpcb);
    static err_t sendStartZMQ(SocketTypes socketType, struct tcp_pcb *tpcb);
    static bool queue_remove_timeout(queue_t *q, void *data, clock_t timout = 5000);

    err_t sendSub(const string& subTopic);
    err_t settingUpTcpPcb();
    err_t closeTcpPcb();
    err_t connectToZmq();
    err_t sendReadyMessage();

    ip_addr_t remote_addr{};
    uint16_t remote_port;
    SocketTypes socketType;
    uint8_t keepAliveTime = 0;

    queue_t receive_queue{};
    string topic;
    vector <string> subTopics;

    bool connected = false;
    uint16_t reconnectCount = 0;
    uint64_t reconnectTimeout = 0;
    uint64_t lastReconnectAttempt = 0;

    struct tcp_pcb *tcp_pcb{};
    struct tcpData{
        SocketTypes *socketType;
        queue_t *receive_queue;
        bool *connected;
    }tcp_data{};

#if DEBUG_MESSAGE
    static void dump_bytes(const uint8_t *bptr, uint32_t len, const SocketTypes *socketType);
#endif
};


#endif //PICO_EPAPER_CODE_ZMQHEADER_H
