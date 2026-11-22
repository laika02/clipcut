#define _POSIX_C_SOURCE 200809L
#include <wayland-client.h>
#include "virtual-pointer.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static struct zwlr_virtual_pointer_manager_v1 *manager;
static void global(void *data,struct wl_registry *registry,uint32_t name,const char *interface,uint32_t version) {
    (void)data;
    if(!strcmp(interface,"zwlr_virtual_pointer_manager_v1"))manager=wl_registry_bind(registry,name,&zwlr_virtual_pointer_manager_v1_interface,version<2?version:2);
}
static void removed(void *data,struct wl_registry *registry,uint32_t name){(void)data;(void)registry;(void)name;}
static const struct wl_registry_listener registry_listener={global,removed};
static uint32_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*1000+t.tv_nsec/1000000);}
int main(int argc,char **argv) {
    if(argc<3)return 2;
    struct sockaddr_un address={.sun_family=AF_UNIX};snprintf(address.sun_path,sizeof address.sun_path,"%s",argv[2]);
    int socket_fd=socket(AF_UNIX,SOCK_STREAM,0);if(socket_fd<0)return 3;
    if(!strcmp(argv[1],"send")) {
        if(argc!=4||connect(socket_fd,(struct sockaddr*)&address,sizeof address)<0){perror("connect");return 4;}
        write(socket_fd,argv[3],strlen(argv[3]));char reply[16];int n=read(socket_fd,reply,sizeof reply);close(socket_fd);return n>0?0:5;
    }
    struct wl_display *display=wl_display_connect(NULL);if(!display)return 6;
    struct wl_registry *registry=wl_display_get_registry(display);wl_registry_add_listener(registry,&registry_listener,NULL);wl_display_roundtrip(display);
    if(!manager)return 7;
    struct zwlr_virtual_pointer_v1 *pointer=zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager,NULL);wl_display_roundtrip(display);
    unlink(address.sun_path);if(bind(socket_fd,(struct sockaddr*)&address,sizeof address)<0||listen(socket_fd,4)<0){perror("listen");return 8;}
    puts("virtual pointer ready");fflush(stdout);
    struct pollfd fds[2]={{socket_fd,POLLIN,0},{wl_display_get_fd(display),POLLIN,0}};
    while(poll(fds,2,-1)>0) {
        if(fds[1].revents&POLLIN)if(wl_display_dispatch(display)<0)break;
        if(fds[0].revents&POLLIN) {
            int client=accept(socket_fd,NULL,NULL);char command[128]={0};read(client,command,sizeof command-1);
            int x,y;double amount;
            if(sscanf(command,"set %d %d",&x,&y)==2)zwlr_virtual_pointer_v1_motion_absolute(pointer,now(),x,y,1920,1080);
            else if(!strcmp(command,"press button1"))zwlr_virtual_pointer_v1_button(pointer,now(),272,WL_POINTER_BUTTON_STATE_PRESSED);
            else if(!strcmp(command,"release button1"))zwlr_virtual_pointer_v1_button(pointer,now(),272,WL_POINTER_BUTTON_STATE_RELEASED);
            else if(sscanf(command,"scroll %lf",&amount)==1){zwlr_virtual_pointer_v1_axis_source(pointer,WL_POINTER_AXIS_SOURCE_FINGER);zwlr_virtual_pointer_v1_axis(pointer,now(),WL_POINTER_AXIS_VERTICAL_SCROLL,wl_fixed_from_double(amount));}
            else if(!strcmp(command,"scroll-end")){zwlr_virtual_pointer_v1_axis_source(pointer,WL_POINTER_AXIS_SOURCE_FINGER);zwlr_virtual_pointer_v1_axis_stop(pointer,now(),WL_POINTER_AXIS_VERTICAL_SCROLL);}
            zwlr_virtual_pointer_v1_frame(pointer);wl_display_roundtrip(display);write(client,"ok",2);close(client);
        }
        if(fds[1].revents&(POLLERR|POLLHUP))break;
    }
    unlink(address.sun_path);close(socket_fd);wl_display_disconnect(display);return 0;
}
