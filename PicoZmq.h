/**
 * @file ZMQ API
 * @author Cederic Nijssen
 * @date 18/03/2023
 */

/*
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documnetation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to  whom the Software is
 * furished to do so, subject to the following conditions:
 * <br><br>
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * <br><br>
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the PicoZMQ Class
 *
 * author: Cederic Nijssen
 */

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

/**
 * @brief PicoZmq class used to create ZMQ sockets. At the moment PUB, SUB, PUSH and PULL are implemented
 */
class PicoZmq{
public:
    /**
     * Struct containing data of message
     */
    struct returnMessage{
        uint8_t topicID;        /**< id of topic in topic vector */
        vector<char> payload;   /**< payload of message */
    };

    /**
     * enum containing implemented socket types
     */
    enum SocketTypes{
        PUB = 0,
        SUB = 1,
        PUSH = 2,
        PULL = 3,
    };

    /**
     * Create new socket
     * @brief Constructor
     * @param remoteAddr Address of ZeroMQ server
     * @param remote_port Port of ZeroMQ server
     * @param socket_type Socket Type
     * @param keepAliveTime Time in s how long the socked need to wait between keep alive messages. No keep alive message when 0.<br> Default value: 0
     */
    PicoZmq(const string& remoteAddr, uint16_t remote_port, SocketTypes socket_type, uint8_t keepAliveTime = 0);

    /**
     * Cleans up tcp socket
     * @brief Destructor
     */
    virtual ~PicoZmq();

    /**
     * Checks if the socket is connected
     * @return Whether the socket is connected
     */
    [[nodiscard]] bool isConnected() const{return connected;}

    /**
     * Checks if there are new messages
     * @return Whether there are new messages waiting for processing
     */
    [[nodiscard]] bool gotMessage() {return !queue_is_empty(&receive_queue);}

    /**
     * Possibility to set publish/push topic to start messages with
     * @param newTopic Topic to be set. <br> Can be left empty
     */
    void setTopic(const string &newTopic){topic = newTopic;}

    /**
     * Send message if PUSH or PUB socket is used
     * @param message string containing the message
     * @return ERR_OK if send, another err_t on error
     * @see sendMessage(const vector<char> &message);
     */
    err_t sendMessage(const string &message);

    /**
     * Send message if PUSH or PUB socket is used
     * @param message vector of chars containing the message
     * @return ERR_OK if send, another err_t on error
     * @see sendMessage(const string &message);
     */
    err_t sendMessage(const vector<char> &message);

    /**
     * Subscribes to topic. Possible to subscribe to multiple topics
     * @param subTopic string with the topic
     * @return ERR_OK if subscribed, another err_t on error
     */
    err_t subscribe(const string &subTopic);

    /**
     * Get first message from que
     * @return Message struct with topic ID and payload
     */
    returnMessage getMessage();

    /**
     * Reconnect if connection is lost
     */
    void reconnect();

#if DEBUG || DEBUG_MESSAGE
    friend ostream& operator<<(ostream& out, PicoZmq::SocketTypes value);
    friend ostream& operator<<(ostream& out,const PicoZmq::SocketTypes *value);
#endif
private:
    /**
     * Callback function when tcp gets an error
     */
    static void tcp_client_err(void *arg, err_t err);

    /**
     * Callback function when tcp receive a message
     */
    static err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

    /**
     * Callback function when tcp is successfully connected
     */
    static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err);

    /**
     * Callback function for tcp polling, Keep alive
     */
    static err_t tcp_client_poll(void *arg, struct tcp_pcb *tpcb);

    /**
     * remove item from the queue with a given timeout
     * @param q pointer to the que
     * @param data pointer to variable to copy the data in
     * @param timout timeout in ms after which the function fails
     * @return true when successfully copied item, false when timeout exceeds
     */
    static bool queue_remove_timeout(queue_t *q, void *data, clock_t timout = 5000);

    /**
     * send subscribe message
     * @param subTopic topic to subscribe to
     * @return ERR_OK when start succesfuly send, another err_t on error
     */
    err_t sendSub(const string& subTopic);

    /**
     * Setting up TCP PCB with earlier given parameters and connect to TCP server
     * @return ERR_OK when successfully connected, another err_t on error
     */
    err_t settingUpTcpPcb();

    /**
     * Close tcp pcb socket
     * @return ERR_OK when successfully closed socket, another err_t on error
     */
    err_t closeTcpPcb();

    /**
     * Connect to ZMQ server with earlier given parameters
     * @return ERR_OK when successfully connected, another err_t on error
     */
    err_t connectToZmq();

    /**
     * Send ZMQ start handshake
     * @param socketType socket type of current socket
     * @param tpcb pointer to current tcp pcb
     * @return ERR_OK when start succesfuly send, another err_t on error
     */
    static err_t sendStartZMQ(SocketTypes socketType, struct tcp_pcb *tpcb);

    /**
     * Send Ready Part of ZMQ handshake
     * @return ERR_OK when start succesfuly send, another err_t on error
     */
    err_t sendReadyMessage();

    /// IP address of ZMQ Server
    ip_addr_t remote_addr{};
    /// Port of ZMQ Server
    uint16_t remote_port;
    /// Socket type of Socket
    SocketTypes socketType;
    /// Time between keep alive/check connection messages
    uint8_t keepAliveTime = 0;

    /// Queue where received messages are put in
    queue_t receive_queue{};
    /// publish prefix
    string topic;
    /// vector with subscribed topics
    vector <string> subTopics;

    /// flag that hold connection status of socket
    bool connected = false;
    /// number of failed attempts in sequence
    uint16_t reconnectCount = 0;
    /// time between reconnection attempts
    uint64_t reconnectTimeout = 0;
    /// time is us of last attempt
    uint64_t lastReconnectAttempt = 0;

    /// tcp pcb socket struct
    struct tcp_pcb *tcp_pcb{};
    /// struct data given to the callback functions
    struct tcpData{
        SocketTypes *socketType;        /// socket type of current socket
        queue_t *receive_queue;         /// que to put received messages in
        bool *connected;                /// the connected variable
    }tcp_data{};

#if DEBUG_MESSAGE
    static void dump_bytes(const uint8_t *bptr, uint32_t len, const SocketTypes *socketType);
#endif
};


#endif //PICO_EPAPER_CODE_ZMQHEADER_H
