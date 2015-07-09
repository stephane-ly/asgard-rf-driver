//=======================================================================
// Copyright (c) 2015 Baptiste Wicht
// Distributed under the terms of the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "RCSwitch.h"

namespace {

const std::size_t UNIX_PATH_MAX = 108;
const std::size_t gpio_pin = 2;

//Buffers
char write_buffer[4096];
char receive_buffer[4096];

bool revoke_root(){
    if (getuid() == 0) {
        if (setgid(1000) != 0){
            std::cout << "asgard:dht11: setgid: Unable to drop group privileges: " << strerror(errno) << std::endl;
            return false;
        }

        if (setuid(1000) != 0){
            std::cout << "asgard:dht11: setgid: Unable to drop user privileges: " << strerror(errno) << std::endl;
            return false;
        }
    }

    if (setuid(0) != -1){
        std::cout << "asgard:dht11: managed to regain root privileges, exiting..." << std::endl;
        return false;
    }

    return true;
}

void decode_wt450(unsigned long value){
    unsigned long data;
    int house=0;
    byte station=0;
    int humidity=0;
    double temperature=0;
    double tempdecimal=0;
    byte tempfraction=0;

    data=(unsigned long)value;

    house=(data>>28) & (0x0f);
    station=((data>>26) & (0x03))+1;
    humidity=(data>>16)&(0xff);
    temperature=((data>>8) & (0xff));
    temperature = temperature - 50;
    tempfraction=(data>>4) & (0x0f);

    tempdecimal=((tempfraction>>3 & 1) * 0.5) + ((tempfraction>>2 & 1) * 0.25) + ((tempfraction>>1 & 1) * 0.125) + ((tempfraction & 1) * 0.0625);
    temperature=temperature+tempdecimal;
    temperature=(int)(temperature*10);
    temperature=temperature/10;

    std::cout << house << std::endl;
    std::cout << station << std::endl;
    std::cout << humidity << std::endl;
    std::cout << temperature << std::endl;
}

void read_data(RCSwitch& rc_switch, int socket_fd, int rf_button_1){
    if (rc_switch.available()) {
        int value = rc_switch.getReceivedValue();

        if (value) {
            if((rc_switch.getReceivedProtocol() == 1 || rc_switch.getReceivedProtocol() == 2) && rc_switch.getReceivedValue() == 1135920){
                //Send the event to the server
                auto nbytes = snprintf(write_buffer, 4096, "EVENT %d 1", rf_button_1);
                write(socket_fd, write_buffer, nbytes);
            } else {
                printf("asgard:rf:received unknown value: %i\n", rc_switch.getReceivedValue());
                printf("asgard:rf:received unknown protocol: %i\n", rc_switch.getReceivedProtocol());

                unsigned long value = rc_switch.getReceivedValue();
                decode_wt450(value);
            }
        } else {
            printf("asgard:rf:received unknown encoding\n");
        }

        rc_switch.resetAvailable();
    }
}

} //end of anonymous namespace

int main(){
    RCSwitch rc_switch;

    //Run the wiringPi setup (as root)
    wiringPiSetup();

    rc_switch = RCSwitch();
    rc_switch.enableReceive(gpio_pin);

    //Drop root privileges and run as pi:pi again
    if(!revoke_root()){
       std::cout << "asgard:dht11: unable to revoke root privileges, exiting..." << std::endl;
       return 1;
    }

    //Open the socket
    auto socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if(socket_fd < 0){
        std::cout << "asgard:rf: socket() failed" << std::endl;
        return 1;
    }

    //Init the address
    struct sockaddr_un address;
    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, UNIX_PATH_MAX, "/tmp/asgard_socket");

    //Connect to the server
    if(connect(socket_fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un)) != 0){
        std::cout << "asgard:rf: connect() failed" << std::endl;
        return 1;
    }

    //Register the actuator
    auto nbytes = snprintf(write_buffer, 4096, "REG_ACTUATOR rf_button_1");
    write(socket_fd, write_buffer, nbytes);

    //Get the response from the server
    nbytes = read(socket_fd, receive_buffer, 4096);

    if(!nbytes){
        std::cout << "asgard:ir: failed to register actuator" << std::endl;
        return 1;
    }

    receive_buffer[nbytes] = 0;

    //Parse the actuator id
    int rf_button_1 = atoi(receive_buffer);
    std::cout << "remote actuator: " << rf_button_1 << std::endl;

    while(true) {
        read_data(rc_switch, socket_fd, rf_button_1);

        delay(100); //TODO Perhaps this is not a good idea
    }

    //Close the socket
    close(socket_fd);

    return 0;
}
