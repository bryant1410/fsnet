//
//  FSClientBase.cpp
//  fsnet_client
//
//  Created by Vincent on 15/5/5.
//  Copyright (c) 2015年 Vincent. All rights reserved.
//

#include "FSClientBase.h"
#include "FSPack.h"

#include <string>
#include <sys/socket.h>

FSClientBase::FSClientBase(const char* host, unsigned short port):port(port){

    memcpy(hostname, host, 16);
    
    this->timeout.tv_sec = 5;
    this->timeout.tv_usec = 0;
    this->socket = -1;
    this->send_stream = fs_create_output_stream_ext;
    this->recv_stream = fs_create_output_stream_ext;
    this->connecting = fs_false;
    
}


fs_bool FSClientBase::tick(float dt) {
    
    if(this->socket == -1){
        return fs_false;
    }
    
    FD_ZERO(&this->socket_write_event);
    FD_SET(this->socket, &this->socket_write_event);
    FD_ZERO(&this->socket_read_event);
    FD_SET(this->socket, &this->socket_read_event);
    FD_ZERO(&this->socket_error_event);
    FD_SET(this->socket, &this->socket_error_event);
    
    int ret = select(this->socket + 1, &this->socket_read_event, &this->socket_write_event, &this->socket_error_event, &this->timeout);
    
    if(ret < 0){
        // error
        
    }else if(ret == 0){
        // timeout
        
    }else{
        
        if(FD_ISSET(this->socket, &this->socket_error_event)){
            
            if(this->connecting){
                this->connecting = fs_false;
                return fs_false;
            }else{
                on_disconnect();
                return fs_false;
            }
        }
        
        int error = 0, error_len;
        getsockopt(this->socket, SOL_SOCKET, SO_ERROR, &error, (socklen_t *)&error_len);
        
        if(error != 0){
            
            if(this->connecting){
                this->connecting = fs_false;
                return fs_false;
            }else{
                on_disconnect();
                return fs_false;
            }
            
            return fs_false;
        }
        
        // connected
        if (FD_ISSET(this->socket, &this->socket_write_event)) {
            
            
            if(this->connecting){
                this->connecting = fs_false;
                on_connect_successfly();
                return fs_true;
            }else{
                
                // try to send
                if(!on_send()){
                    return fs_false;
                }
                
            }
        }
        
        // can be read
        if (FD_ISSET(this->socket, &this->socket_read_event)){
            
            
            if(!on_recv()){
                return fs_false;
            }
            
        }
    }
    
    return fs_true;

}







fs_bool FSClientBase::connect(){

    
    this->socket = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    this->addr.sin_family = AF_INET;
    this->addr.sin_port = htons(port);
    this->addr.sin_addr.s_addr = inet_addr(hostname);
    fs_zero(this->addr.sin_zero, sizeof(this->addr.sin_zero));
    
    
    
    int fdflags = fcntl(this->socket, F_GETFL, 0);
    if(fcntl(this->socket, F_SETFL, fdflags | O_NONBLOCK) < 0){
        return fs_false;
    }
    
    if(::connect(this->socket, (struct sockaddr *)&this->addr, sizeof(struct sockaddr_in)) == -1){
        if(errno == EISCONN){
            on_connect_successfly();
            return fs_true;
        }
        this->connecting = fs_true;
        if(!tick(0)){
            on_connect_faild();
            return fs_false;
        }
        
    }else{
        on_connect_faild();
    }
    
    
    return fs_true;

    
}

fs_bool FSClientBase::on_send(){
 
    if(fs_output_stream_get_len(this->send_stream) > 0){
        
        void* data = (void*)fs_output_stream_get_dataptr(this->send_stream);
        size_t len = fs_output_stream_get_len(this->send_stream);
        ssize_t nsize = len;
        ssize_t nwrite = 0;
        ssize_t nwrited = 0;
        
        // 每次tick 只发一次
        nwrite = ::send(this->socket, (BYTE*)data + (len - nsize), nsize, 0);
        if(nwrite > 0){
            nwrited += nwrite;
        }
        if(nwrite < nsize){
            if(nwrite == -1){
                if(errno == EAGAIN)
                {
                    
                }else{
                    on_disconnect();
                    return fs_false;
                }
            }
        }
        
        fs_output_stream_sub(this->send_stream, nwrited, len - nwrited);
        
        return fs_true;
        
    }
    return fs_true;
    
}

fs_bool FSClientBase::on_recv(){
    
    
    BYTE buffer[1024];
    ssize_t len = recv(this->socket, buffer, 1024, 0);
    size_t buff_len = 0;
    FSPack pack;
    
    if(len > 0){
        
        fs_stream_write_data(this->recv_stream, buffer, len);
        
        while(on_parse_pack(&pack)){
            
            on_dispatch_pack(&pack);
            
            buff_len = fs_output_stream_get_len(this->recv_stream);
            
            fs_output_stream_sub(this->recv_stream, pack.length(), buff_len - pack.length());
            
            
        }
        
    }else{
        if(len == -1){
            if(errno == EAGAIN){
                
            }else{
                on_disconnect();
                return fs_false;
            }
        }else{
            on_disconnect();
            return fs_false;
        }
    }
    
    
    return fs_true;
    
}

fs_bool FSClientBase::on_parse_pack(FSPack* in){
    
    struct fs_input_stream* fis = fs_create_input_stream(
                                                         fs_output_stream_get_dataptr(this->recv_stream),
                                                         fs_output_stream_get_len(this->recv_stream));
    
    
    fs_bool ret = in->read_from_stream(fis);
    
    fs_stream_free_input(fis);
    

    return ret;
}


fs_bool FSClientBase::connected(){
    return (!this->connecting && this->socket != -1);
}

fs_bool FSClientBase::send_pack(FSPack* pack){
    
    pack->write_to_stream(this->send_stream);
    
    return fs_true;
}

void FSClientBase::on_connect_successfly(){
    
}

void FSClientBase::on_connect_faild(){
    
    if(this->socket != -1){
        ::close(socket);
    }
}
void FSClientBase::on_disconnect(){
    if(this->socket != -1){
        ::close(socket);
    }
    
}


fs_bool FSClientBase::on_dispatch_pack(FSPack* pack){
    fs_assert(false, "on_dispatch_pack not impl");
    return true;
}
