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

#include "PicoZmq.h"

#define CLEAR_ARRAY(array) (fill_n(array, 255, 0))

PicoZmq::PicoZmq(const string& remoteAddr, uint16_t remote_port, SocketTypes socket_type, uint8_t keep_alive_time): remote_port(remote_port), socketType(socket_type) {
    if(keep_alive_time > 127){
        COUT(socketType << "Keep alive time must be less then or equal to 127");
        return;
    }
    keepAliveTime = keep_alive_time * 2;

    queue_init(&receive_queue, sizeof(char[255]), 4);

    ip4addr_aton(remoteAddr.c_str(), &remote_addr);

    err_t err = settingUpTcpPcb();
    if (err != ERR_OK){
        COUT(socketType << "error setting up tcp pcb err code: " << (int) err << endl);
        return;
    }

    err = connectToZmq();
    if (err != ERR_OK){
        COUT(socketType << "error connecting to ZMQ err code: " << (int) err << endl);
        return;
    }
}

PicoZmq::~PicoZmq() {
    if(tcp_pcb != nullptr){
        tcp_arg(tcp_pcb, nullptr);
        tcp_poll(tcp_pcb, nullptr, 0);
        tcp_sent(tcp_pcb, nullptr);
        tcp_recv(tcp_pcb, nullptr);
        tcp_err(tcp_pcb, nullptr);

        cyw43_arch_lwip_begin();
        err_t err = tcp_close(tcp_pcb);
        cyw43_arch_lwip_end();

        if(err != ERR_OK){
            COUT(socketType << "closing tcp socket failed" << endl);
            cyw43_arch_lwip_begin();
            tcp_abort(tcp_pcb);
            cyw43_arch_lwip_end();
        }
        tcp_pcb = nullptr;
    }
}

err_t PicoZmq::sendMessage(const string &message) {
    if(socketType == PUB || socketType == PUSH){
        char sentData[255] = {0x00};
        uint16_t messageSize = topic.size() + message.size();
        if (messageSize > 253){
            COUT(socketType << "message to long: " << (int) messageSize << " > " << 253);
            return ERR_VAL;
        }
        sentData[1] = messageSize;
        strcpy(sentData + 2, topic.c_str());
        strcpy(sentData + 2 + topic.size(), message.c_str());
        COUT_MESSAGE(socketType << "sending " << endl);
        DUMP_MESSAGE_BYTES((uint8_t*) sentData, messageSize + 2, &socketType);

        cyw43_arch_lwip_begin();
        err_t err = tcp_write(tcp_pcb, sentData, messageSize + 2, TCP_WRITE_FLAG_COPY);
        tcp_output(tcp_pcb);
        cyw43_arch_lwip_end();
        return err;
    }
}

err_t PicoZmq::sendMessage(const vector<char> &message) {
    if(socketType == PUB || socketType == PUSH) {
        char sentData[255] = {0x00};
        uint16_t messageSize = topic.size() + message.size();
        if (messageSize > 253) {
            COUT(socketType << "message to long: " << (int) messageSize << " > " << 253);
            return ERR_VAL;
        }
        sentData[1] = messageSize;
        strcpy(sentData + 2, topic.c_str());
        memcpy(sentData + 2 + topic.size(), message.data(), message.size());
        COUT_MESSAGE(socketType << "sending " << endl);
        DUMP_MESSAGE_BYTES((uint8_t *) sentData, messageSize + 2, &socketType);

        cyw43_arch_lwip_begin();
        err_t err = tcp_write(tcp_pcb, sentData, messageSize + 2, TCP_WRITE_FLAG_COPY);
        tcp_output(tcp_pcb);
        cyw43_arch_lwip_end();
        return err;
    }
}

err_t PicoZmq::subscribe(const string &subTopic) {
    if(find(subTopics.begin(), subTopics.end(), subTopic) != subTopics.end()){
        COUT(socketType << "Already subscribed to topic" << endl);
        return ERR_VAL;
    }
    if (isConnected()){
        err_t err = sendSub(subTopic);
        if(err == ERR_OK){
            subTopics.push_back(subTopic);
        }
        return err;
    }
    return ERR_CONN;
}

PicoZmq::returnMessage PicoZmq::getMessage() {
    if(!gotMessage()){return {};}
    vector<char> rec_data_tmp (255, 0);
    queue_try_remove(&receive_queue, rec_data_tmp.data());
    string message(rec_data_tmp.begin() + 2, rec_data_tmp.begin() + rec_data_tmp[1] + 2);
    for (uint8_t i = 0; i < (uint8_t) subTopics.size(); ++i) {
        if(message.find(subTopics[i]) != string::npos){
            vector<char> payload(rec_data_tmp.begin() + (int) subTopics[i].size() + 2, rec_data_tmp.begin() + rec_data_tmp[1] + 2);
            return {i, payload};
        }
    }
    return {};
}

void PicoZmq::reconnect() {
    if((time_us_64() - lastReconnectAttempt > reconnectTimeout && !connected)){
        COUT(socketType << "reconnecting tries: " << reconnectCount + 1 << endl);
        reconnectCount ++;

        err_t err = closeTcpPcb();
        if (err != ERR_OK){
            COUT(socketType << "error closing up tcp pcb, err code: " << (int) err << " aborting tcp pcb" << endl);
            cyw43_arch_lwip_begin();
            tcp_abort(tcp_pcb);
            cyw43_arch_lwip_end();
        }

        err = settingUpTcpPcb();
        if (err != ERR_OK){
            COUT(socketType << "error setting up tcp pcb err code: " << (int) err << endl);
            lastReconnectAttempt = time_us_64();
            reconnectTimeout = RECONNECT_DEFAULT_TIMEOUT * (reconnectCount < 12 ? reconnectCount : 12);
            return;
        }

        err = connectToZmq();
        if (err != ERR_OK){
            COUT(socketType << "error connecting to ZMQ err code: " << (int) err << endl);
            lastReconnectAttempt = time_us_64();
            reconnectTimeout = RECONNECT_DEFAULT_TIMEOUT * (reconnectCount < 12 ? reconnectCount : 12);
            return;
        }

        if(socketType == SUB && connected){
            COUT(socketType << "sending sub message" << endl);
            for (auto &subTopic: subTopics) {
                err = sendSub(subTopic);
                if(err != ERR_OK){
                    return;
                }
            }
        }
    }
}

void PicoZmq::tcp_client_err(void *arg, err_t err) {
    auto *tcp_data = (tcpData*) arg;
    cout << tcp_data->socketType << "tcp client err: " << (int) err << endl;
    *tcp_data->connected = false;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
}

err_t PicoZmq::tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    auto *tcp_data = (tcpData*) arg;
    if(p){
        cyw43_arch_lwip_check();
        if (p->tot_len > 0){
            COUT_MESSAGE(tcp_data->socketType << "recv " << (int) p->tot_len << " bytes with err " << (int) err << endl);
            for (struct pbuf *q = p; q != nullptr; q = q->next){
                DUMP_MESSAGE_BYTES((uint8_t*) q->payload, q->len, tcp_data->socketType);
            }
            char rec_data_tmp[255] = {0};
            pbuf_copy_partial(p, rec_data_tmp, p->tot_len, 0);
            if(! queue_try_add(tcp_data->receive_queue, rec_data_tmp)){COUT(tcp_data->socketType << "failed to add to que" << endl);}
            tcp_recved(tpcb, p->tot_len);
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

err_t PicoZmq::tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    auto *tcp_data = (tcpData*) arg;
    if (err != ERR_OK) {
        COUT(tcp_data->socketType << "connect failed, error number: " << err << endl);
        return ERR_CONN;
    }
    return sendStartZMQ(*tcp_data->socketType, tpcb);
}

err_t PicoZmq::tcp_client_poll(void *arg, struct tcp_pcb *tpcb) {
    auto *tcp_data = (tcpData*) arg;
    err_t err = tcp_write(tpcb,"\0\0", 2, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    COUT(tcp_data->socketType << "POLL function return code: " << (int) err << endl);
    return err;
}

bool PicoZmq::queue_remove_timeout(queue_t *q, void *data, clock_t timout) {
    clock_t startTime = time_us_64();
    timout *= 1000;
    while (!queue_try_remove(q, data)){
        if(time_us_64() - startTime > timout){
            return false;
        }
        sleep_ms(50);
    }
    return true;
}

err_t PicoZmq::sendSub(const string& subTopic) {
    uint16_t subTopicSize = subTopic.size() + 10;
    if (subTopicSize > 253){
        COUT(socketType << "message to long: " << (int) subTopicSize << " > " << 253);
        return ERR_VAL;
    }
    char sentData[255] = {0x00};
    sentData[0] = 0x04;
    sentData[1] = subTopicSize;
    sentData[2] = 0x09;
    sentData[3] = 'S'; sentData[4] = 'U'; sentData[5] = 'B'; sentData[6] = 'S'; sentData[7] = 'C'; sentData[8] = 'R'; sentData[9] = 'I'; sentData[10] = 'B'; sentData[11] = 'E';
    strcpy(sentData + 12, subTopic.c_str());
    COUT_MESSAGE(socketType << "sending:");
    DUMP_MESSAGE_BYTES((uint8_t*) sentData, subTopicSize + 2, &socketType);

    cyw43_arch_lwip_begin();
    err_t err = tcp_write(tcp_pcb, sentData, subTopicSize + 2, TCP_WRITE_FLAG_COPY);
    tcp_output(tcp_pcb);
    cyw43_arch_lwip_end();
    return err;
}

err_t PicoZmq::settingUpTcpPcb() {
    COUT(socketType << "Connecting to " << ip4addr_ntoa(&remote_addr) << ":" << (int) remote_port << endl);
    tcp_pcb = tcp_new_ip_type(IP_GET_TYPE(remote_addr));

    tcp_data.receive_queue = &receive_queue;
    tcp_data.socketType = &socketType;
    tcp_data.connected = &connected;

    tcp_arg(tcp_pcb, &tcp_data);
    tcp_recv(tcp_pcb, tcp_client_recv);
    tcp_err(tcp_pcb, tcp_client_err);
    if(keepAliveTime != 0){
        tcp_poll(tcp_pcb, &tcp_client_poll, keepAliveTime);
    }

    cyw43_arch_lwip_begin();
    err_t err = tcp_connect(tcp_pcb, &remote_addr, remote_port, tcp_client_connected);
    cyw43_arch_lwip_end();
    return err;
}

err_t PicoZmq::closeTcpPcb() {
    cyw43_arch_lwip_begin();
    err_t err = tcp_close(tcp_pcb);
    cyw43_arch_lwip_end();
    return err;
}

err_t PicoZmq::connectToZmq() {
    char rec_data_tmp[255];
    //greeting
    if(! queue_remove_timeout(&receive_queue, &rec_data_tmp)){
        connected = false;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        return ERR_TIMEOUT;
    }
    if(!(rec_data_tmp[12] == 'N' && rec_data_tmp[13] == 'U' && rec_data_tmp[14] == 'L' && rec_data_tmp[15] == 'L')){
        CLEAR_ARRAY(rec_data_tmp);
        if(! queue_remove_timeout(&receive_queue, &rec_data_tmp)){
            connected = false;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
            return ERR_TIMEOUT;
        }
        if(!(rec_data_tmp[2] == 'N' && rec_data_tmp[3] == 'U' && rec_data_tmp[4] == 'L' && rec_data_tmp[5] == 'L')){
            COUT(socketType << "received wrong greeting" << endl);
            connected = false;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
            return ERR_CONN;
        }
    }

    //ready
    CLEAR_ARRAY(rec_data_tmp);
    if(! queue_remove_timeout(&receive_queue, &rec_data_tmp)){
        connected = false;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        return ERR_TIMEOUT;
    }
    if(rec_data_tmp[0] != 0x04 || ! (rec_data_tmp[2] == 0x05 && rec_data_tmp[3] == 'R' && rec_data_tmp[4] == 'E' && rec_data_tmp[5] == 'A' && rec_data_tmp[6] == 'D' && rec_data_tmp[7] == 'Y') || ! (rec_data_tmp[8] == 0x0b && rec_data_tmp[9] == 'S' && rec_data_tmp[10] == 'o' && rec_data_tmp[11] == 'c' && rec_data_tmp[12] == 'k' && rec_data_tmp[13] == 'e' && rec_data_tmp[14] == 't' && rec_data_tmp[15] == '-' && rec_data_tmp[16] == 'T' && rec_data_tmp[17] == 'y' && rec_data_tmp[18] == 'p' && rec_data_tmp[19] == 'e')){
        COUT(socketType << "socket not ready" << endl);
        connected = false;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        return ERR_CONN;
    }

    string socketTypeRec(rec_data_tmp + 24, rec_data_tmp[23]);
//    cout << "socket type server: " << socketTypeRec << endl;

    if(socketTypeRec != names[socketType % 2 ? socketType - 1: socketType + 1]){
        COUT(socketType << "wrong socket pair: server is " << socketTypeRec << " client is " << names[socketType] << endl);
        connected = false;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        return ERR_CONN;
    }

    err_t sendReadyError = sendReadyMessage();
    if(sendReadyError != ERR_OK){
        COUT(socketType << "could not send ready message. Error code: " << sendReadyError << endl);
        connected = false;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        return ERR_CONN;
    }

    COUT(socketType << "connected to ZMQ broker" << endl);
    connected = true;
    reconnectCount = 0;
    return ERR_OK;
}

err_t PicoZmq::sendStartZMQ(PicoZmq::SocketTypes socketType, struct tcp_pcb *tpcb) {
    char data[64] = {0x00};
    data[0] = 0xFF;
    data[9] = 0x7F;
    data[10] = 0x03;
    data[11] = 0x71;
    data[12] = 'N';
    data[13] = 'U';
    data[14] = 'L';
    data[15] = 'L';

    cyw43_arch_lwip_begin();
    err_t err = tcp_write(tpcb, data, sizeof(data), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    cyw43_arch_lwip_end();
    if (err != ERR_OK) {
        COUT(socketType << "Failed to write data. error number: " << (int) err << endl);
        return ERR_ABRT;
    }
    return err;
}

err_t PicoZmq::sendReadyMessage() {
    char dataSocketType[28] = {0x00};
    dataSocketType[0] = 0x04;
    dataSocketType[1] = 25 + (socketType >= 2);
    dataSocketType[2] = 0x05;
    dataSocketType[3] = 'R'; dataSocketType[4] = 'E'; dataSocketType[5] = 'A'; dataSocketType[6] = 'D'; dataSocketType[7] = 'Y';
    dataSocketType[8] = 0x0b;
    dataSocketType[9] = 'S'; dataSocketType[10] = 'o'; dataSocketType[11] = 'c'; dataSocketType[12] = 'k'; dataSocketType[13] = 'e'; dataSocketType[14] = 't'; dataSocketType[15] = '-'; dataSocketType[16] = 'T'; dataSocketType[17] = 'y'; dataSocketType[18] = 'p'; dataSocketType[19] = 'e';
    dataSocketType[23] = 3 + (socketType >= 2);
    dataSocketType[24] = names[socketType][0];
    dataSocketType[25] = names[socketType][1];
    dataSocketType[26] = names[socketType][2];
    if(socketType >= 2){dataSocketType[27] = names[socketType][3];}
    COUT_MESSAGE(socketType << "send ready message: " << endl);
    DUMP_MESSAGE_BYTES((uint8_t*) dataSocketType, 27 + (socketType >= 2), &socketType);

    cyw43_arch_lwip_begin();
    err_t err = tcp_write(tcp_pcb, dataSocketType, 27 + (socketType >= 2), TCP_WRITE_FLAG_COPY);
    tcp_output(tcp_pcb);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        COUT(socketType << "Failed to write data. error number: " << (int) err << endl);
        return ERR_ABRT;
    }
    return err;
}

#if DEBUG_MESSAGE
void PicoZmq::dump_bytes(const uint8_t *bptr, const uint32_t len, const SocketTypes *socketType) {
    cout << socketType << "debug bytes " << len ;
    for (uint32_t i = 0; i < len; ++i) {
        if ((i & 0x0f) == 0) {
            cout << endl << "\t";
        }
        if(bptr[i] >= 32 && bptr[i] <= 126){
            cout << bptr[i];
        }
        else{
            cout << "0x" << setfill('0') << setw(2) << hex <<(int) bptr[i] << dec << ", ";
        }
    }
    cout << endl;
}
#endif

#if DEBUG || DEBUG_MESSAGE
ostream& operator<<(ostream& out, const PicoZmq::SocketTypes value){
    return out << names[value] << ": ";
}

ostream& operator<<(ostream& out, const PicoZmq::SocketTypes *value){
    return out << names[*value] << ": ";
}

#endif
