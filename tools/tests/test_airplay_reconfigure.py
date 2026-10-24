"""Run real AirPlay reconfiguration/lifecycle functions with deterministic OS mocks.

These tests compile production function bodies. No device access is involved.
Direct run: python tools/tests/test_airplay_reconfigure.py
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

try:
    from .test_airplay_mode import ROOT, PORT, compiler_command, function
except ImportError:
    from test_airplay_mode import ROOT, PORT, compiler_command, function


MODE_PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_TIMEOUT -3
#define ESP_ERR_NO_MEM -4
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_RETURN_ON_ERROR(expr,...) do { esp_err_t _err=(expr); if(_err!=ESP_OK) return _err; } while(0)
#define ESP_RETURN_ON_FALSE(expr,err,...) do { if(!(expr)) return err; } while(0)
#define AIRPLAY2_EV_DISCONNECTED 1
static struct {
    atomic_bool started;
    bool initialized;
    char name[65];
    uint8_t mac[6];
    float volume_db;
    bool artwork;
    atomic_bool legacy_mode;
    void *listener;
    void *ctx;
    volatile bool connected, playing;
} s;
static bool listener_alive;
static int stops, starts, forbidden_calls;
static int stop_errors[8], start_errors[8];
static char trace[80];
static size_t trace_len;
static void record(char action) {
    assert(trace_len+2<sizeof(trace));
    trace[trace_len++]=action;
    trace[trace_len++]=atomic_load(&s.legacy_mode) ? '1' : '0';
    trace[trace_len]=0;
}
static esp_err_t rtsp_server_stop_checked(void) {
    record('S'); int error=stop_errors[stops++];
    if(error==ESP_OK) listener_alive=false;
    return error;
}
static esp_err_t rtsp_server_start(void) {
    record('N');
    /* No new listener or mode rollback may overlap an unjoined prior listener. */
    assert(!listener_alive);
    listener_alive=true; /* Even a timed-out start may still own a task. */
    return start_errors[starts++];
}
static const char *esp_err_to_name(int error) { (void)error; return "mock"; }
static void emit(int event,void *data) { (void)event;(void)data; }
static int hap_init(void) { ++forbidden_calls; return ESP_OK; }
static int ptp_clock_init(void) { ++forbidden_calls; return ESP_OK; }
static int audio_receiver_init(void) { ++forbidden_calls; return ESP_OK; }
static int audio_output_init(void) { ++forbidden_calls; return ESP_OK; }
static void mdns_airplay_init(void) { ++forbidden_calls; }
static void mdns_airplay_remove(void) { ++forbidden_calls; }
static void airplay2_advertise(void) { ++forbidden_calls; }
static void airplay2_unadvertise(void) { ++forbidden_calls; }
static void reset(void) {
    memset(&s,0,sizeof(s));
    atomic_store(&s.started,true); atomic_store(&s.legacy_mode,false);
    s.initialized=true; s.connected=true; s.playing=true;
    strcpy(s.name,"P4 persistent identity"); s.mac[0]=0x80;
    s.volume_db=-12.5f; s.artwork=true; s.ctx=(void*)(uintptr_t)0x1234;
    listener_alive=true; stops=starts=forbidden_calls=0;
    memset(stop_errors,0,sizeof(stop_errors)); memset(start_errors,0,sizeof(start_errors));
    trace_len=0; trace[0]=0;
}
'''

MODE_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==2); int which=atoi(argv[1]); reset();
    switch(which) {
    case 0: /* Reconfiguration cannot initialize a receiver on behalf of its owner. */
        s.initialized=false; atomic_store(&s.started,false); listener_alive=false;
        assert(airplay2_set_legacy_mode(true)==ESP_ERR_INVALID_STATE);
        assert(!strcmp(trace,"") && !airplay2_legacy_mode());
        break;
    case 1: /* Selecting current, healthy mode leaves playback alone. */
        assert(airplay2_set_legacy_mode(false)==ESP_OK);
        assert(!strcmp(trace,"") && s.connected && s.playing && atomic_load(&s.started));
        break;
    case 2: /* Quiesce before changing identity and opening the new port. */
        assert(airplay2_set_legacy_mode(true)==ESP_OK);
        assert(!strcmp(trace,"S0N1") && airplay2_legacy_mode() && atomic_load(&s.started));
        break;
    case 3: /* Live-task timeout must not change the mode or create a second listener. */
        stop_errors[0]=ESP_ERR_TIMEOUT;
        assert(airplay2_set_legacy_mode(true)==ESP_ERR_TIMEOUT);
        assert(!strcmp(trace,"S0") && !airplay2_legacy_mode() && !atomic_load(&s.started));
        assert(listener_alive && starts==0);
        break;
    case 4: /* Failed new listener is joined before an old-mode rollback. */
        start_errors[0]=ESP_FAIL;
        assert(airplay2_set_legacy_mode(true)!=ESP_OK);
        assert(!strcmp(trace,"S0N1S1N0"));
        assert(!airplay2_legacy_mode() && atomic_load(&s.started) && listener_alive);
        break;
    case 5: /* A failed cleanup must retain target identity until that task exits. */
        start_errors[0]=ESP_FAIL; stop_errors[1]=ESP_ERR_TIMEOUT;
        assert(airplay2_set_legacy_mode(true)==ESP_ERR_TIMEOUT);
        assert(!strcmp(trace,"S0N1S1") && airplay2_legacy_mode());
        assert(!atomic_load(&s.started) && listener_alive && starts==1);
        break;
    case 6: /* Rollback failure remains unavailable and attempts checked cleanup. */
        start_errors[0]=ESP_FAIL; start_errors[1]=ESP_ERR_NO_MEM;
        assert(airplay2_set_legacy_mode(true)!=ESP_OK);
        assert(!strcmp(trace,"S0N1S1N0S0"));
        assert(!airplay2_legacy_mode() && !atomic_load(&s.started) && !listener_alive);
        break;
    case 7: /* Same mode is retried when listener is unavailable. */
        atomic_store(&s.started,false); listener_alive=false;
        assert(airplay2_set_legacy_mode(false)==ESP_OK);
        assert(!strcmp(trace,"S0N0") && atomic_load(&s.started) && !airplay2_legacy_mode());
        break;
    case 8: /* Both directions work using the existing initialized receiver. */
        assert(airplay2_set_legacy_mode(true)==ESP_OK);
        assert(airplay2_set_legacy_mode(false)==ESP_OK);
        assert(!strcmp(trace,"S0N1S1N0") && atomic_load(&s.started) && !airplay2_legacy_mode());
        break;
    case 9: /* A timeout is recoverable only after a later checked join succeeds. */
        stop_errors[0]=ESP_ERR_TIMEOUT;
        assert(airplay2_set_legacy_mode(true)==ESP_ERR_TIMEOUT);
        assert(airplay2_set_legacy_mode(true)==ESP_OK);
        assert(!strcmp(trace,"S0S0N1") && atomic_load(&s.started) && airplay2_legacy_mode());
        break;
    default: return 99;
    }
    assert(forbidden_calls==0);
    assert(!strcmp(s.name,"P4 persistent identity") && s.mac[0]==0x80);
    assert(s.volume_db==-12.5f && s.artwork && s.ctx==(void*)(uintptr_t)0x1234);
    return 0;
}
'''


def build_program(directory, name, source):
    path=directory/(name+".c")
    path.write_text(source,encoding="utf-8")
    executable=directory/(name+(".exe" if os.name=="nt" else ""))
    command,env=compiler_command(path,executable)
    if Path(command[0]).name.lower()=="cl.exe":
        command.append("/experimental:c11atomics")
    compiled=subprocess.run(command,cwd=directory,env=env,capture_output=True,text=True,timeout=120)
    if compiled.returncode:
        raise AssertionError(compiled.stdout+compiled.stderr)
    return executable,env


class AirplayReconfigureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="airplay-reconfigure-")
        cls.addClassCleanup(cls.temp.cleanup)
        source=(PORT/"airplay2.c").read_text(encoding="utf-8")
        start=source.index("bool airplay2_legacy_mode(")
        getter=source[start:source.index("}",start)+1]
        switch=function(source,"esp_err_t airplay2_set_legacy_mode(")
        cls.executable,cls.env=build_program(Path(cls.temp.name),"reconfigure",
                                            MODE_PREFIX+getter+"\n"+switch+MODE_MAIN)

    def run_case(self,number):
        result=subprocess.run([str(self.executable),str(number)],env=self.env,
                              capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_uninitialized_receiver_is_rejected(self): self.run_case(0)
    def test_current_healthy_mode_preserves_playback(self): self.run_case(1)
    def test_new_mode_only_after_checked_stop(self): self.run_case(2)
    def test_stop_timeout_preserves_mode_and_disables_advertising(self): self.run_case(3)
    def test_new_listener_failure_rolls_back_after_join(self): self.run_case(4)
    def test_failed_join_prevents_rollback_under_live_task(self): self.run_case(5)
    def test_failed_rollback_listener_stays_unavailable(self): self.run_case(6)
    def test_same_mode_retries_unavailable_listener(self): self.run_case(7)
    def test_round_trip_preserves_initialized_resources(self): self.run_case(8)
    def test_retry_after_stop_timeout_requires_later_join(self): self.run_case(9)



SERVER_PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <setjmp.h>
#include <errno.h>
#include "mode_policy.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_TIMEOUT -3
#define ESP_ERR_NO_MEM -4
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef void *TaskHandle_t;
typedef struct { int held; } mutex_t;
typedef mutex_t *SemaphoreHandle_t;
typedef struct { int unused; } rtsp_conn_t;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY ((unsigned)-1)
#define pdMS_TO_TICKS(n) (n)
#define SHUT_RDWR 2
#define AF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define INADDR_ANY 0
#define F_GETFL 1
#define F_SETFL 2
#define O_NONBLOCK 4
#define SERVER_STACK_SIZE 4096
#define CLIENT_STACK_SIZE 8192
#define RTSP_PORT airplay_mode_rtsp_port(airplay2_port_legacy())
typedef unsigned socklen_t;
struct sockaddr { int unused; };
struct sockaddr_in { int sin_family; struct { uint32_t s_addr; } sin_addr; uint16_t sin_port; };
struct timeval { long tv_sec,tv_usec; };
typedef struct { unsigned mask; } fd_set;
#define FD_ZERO(p) ((p)->mask=0)
#define FD_SET(fd,p) ((p)->mask|=1)
static uint16_t htons(uint16_t n) { return n; }
static uint32_t htonl(uint32_t n) { return n; }
static bool host_legacy;
static bool airplay2_port_legacy(void) { return host_legacy; }
static SemaphoreHandle_t xSemaphoreCreateMutex(void);
static int xSemaphoreTake(SemaphoreHandle_t,unsigned);
static void xSemaphoreGive(SemaphoreHandle_t);
static void vSemaphoreDelete(SemaphoreHandle_t);
static void vTaskDelay(unsigned);
static void vTaskDelete(void *);
static BaseType_t xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
static int socket(int,int,int);
static int shutdown(int,int);
static int close(int);
static int setsockopt(int,int,int,const void *,size_t);
static int bind(int,const struct sockaddr *,size_t);
static int listen(int,int);
static int fcntl(int,int,int);
static int select(int,fd_set *,void *,void *,struct timeval *);
static int accept(int,struct sockaddr *,socklen_t *);
static void client_task(void *p) { (void)p; assert(!"Unexpected client create"); }
static void server_task(void *);
'''

SERVER_MOCKS = r'''
static mutex_t locks[8];
static int lock_count,create_lock_fail_at,task_creates,task_create_error;
static int delays,run_after_delays,client_done_after;
static bool allow_task_join,in_owner;
static int socket_error,bind_error,listen_error,fcntl_error;
static int socket_calls,bind_calls,listen_calls,close_calls,shutdown_calls;
static int last_bound_port;
static jmp_buf task_yield;
static void run_owner(void) {
    in_owner=true;
    if(setjmp(task_yield)==0) server_task(NULL);
    in_owner=false;
}
static SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    ++lock_count;
    if(lock_count==create_lock_fail_at) return NULL;
    assert(lock_count<8); return &locks[lock_count];
}
static int xSemaphoreTake(SemaphoreHandle_t p,unsigned ticks) {
    (void)ticks; assert(p && !p->held); p->held=1; return pdTRUE;
}
static void xSemaphoreGive(SemaphoreHandle_t p) { assert(p && p->held); p->held=0; }
static void vSemaphoreDelete(SemaphoreHandle_t p) { (void)p; }
static BaseType_t xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,
                              void *arg,unsigned priority,TaskHandle_t *handle) {
    (void)stack;(void)arg;(void)priority;
    assert(fn==server_task && !strcmp(name,"rtsp_server") && handle==NULL);
    assert(server_task_active); /* Reserved before scheduling the task. */
    ++task_creates;
    if(task_create_error) return 0;
    if(run_after_delays==0) run_owner();
    return pdPASS;
}
static void vTaskDelay(unsigned ms) {
    (void)ms; ++delays;
    if(server_task_active &&
       ((server_stop_requested && allow_task_join) ||
        (!server_stop_requested && run_after_delays>0 && delays>=run_after_delays))) {
        run_owner();
    }
    if(client_done_after>0 && delays>=client_done_after) {
        for(int i=0;i<2;++i) {
            if(clients[i].active) {
                assert(clients[i].should_stop);
                in_owner=true; close_owned_socket(&clients[i].socket); in_owner=false;
                clients[i].conn=NULL; clients[i].active=false;
            }
        }
    }
}
static void vTaskDelete(void *p) { (void)p; }
static int socket(int family,int kind,int protocol) {
    assert(family==AF_INET && kind==SOCK_STREAM && protocol==IPPROTO_TCP);
    ++socket_calls; return socket_error ? -1 : 20;
}
static int shutdown(int fd,int how) { assert(fd>=0 && how==SHUT_RDWR); ++shutdown_calls; return 0; }
static int close(int fd) {
    assert(in_owner && fd>=0); /* Stop must not close/reuse another task's descriptor. */
    ++close_calls; return 0;
}
static int setsockopt(int fd,int level,int opt,const void *val,size_t size) {
    (void)fd;(void)level;(void)opt;(void)val;(void)size; return 0;
}
static int bind(int fd,const struct sockaddr *address,size_t size) {
    (void)fd;(void)size; ++bind_calls;
    last_bound_port=((const struct sockaddr_in*)address)->sin_port;
    return bind_error ? -1 : 0;
}
static int listen(int fd,int count) { (void)fd;(void)count; ++listen_calls; return listen_error ? -1 : 0; }
static int fcntl(int fd,int command,int flags) {
    (void)fd;(void)flags; return fcntl_error==command ? -1 : 0;
}
static int select(int nfds,fd_set *fds,void *w,void *e,struct timeval *wait) {
    (void)nfds;(void)fds;(void)w;(void)e;
    assert(wait->tv_sec==0 && wait->tv_usec>0 && wait->tv_usec<=200000);
    /* Suspend at the real accept loop after bind/listen and readiness publication.
       Cancellation later re-enters the owner's real early-stop cleanup path. */
    longjmp(task_yield,1);
    return 0;
}
static int accept(int fd,struct sockaddr *addr,socklen_t *size) {
    (void)fd;(void)addr;(void)size; assert(!"select should suspend before accept"); return -1;
}
static void reset_server(void) {
    memset(locks,0,sizeof(locks)); atomic_store(&lifecycle_mutex,NULL);
    socket_mutex=cleanup_mutex=NULL; atomic_store(&conn_mutex,NULL); server_socket=-1;
    server_task_active=false; server_running=false; server_stop_requested=false;
    server_start_result=ESP_FAIL;
    for(int i=0;i<2;++i) {
        clients[i].conn=NULL; clients[i].active=false; clients[i].socket=-1;
        clients[i].should_stop=false; clients[i].is_old=false;
    }
    lock_count=create_lock_fail_at=task_creates=task_create_error=0;
    delays=run_after_delays=client_done_after=0;
    allow_task_join=true; in_owner=false; host_legacy=false;
    socket_error=bind_error=listen_error=fcntl_error=0;
    socket_calls=bind_calls=listen_calls=close_calls=shutdown_calls=last_bound_port=0;
}
'''

SERVER_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==2); reset_server(); int which=atoi(argv[1]);
    switch(which) {
    case 0: /* Start success is bind/listen readiness, not merely task creation. */
        run_after_delays=3;
        assert(rtsp_server_start()==ESP_OK);
        assert(delays==3 && bind_calls==1 && listen_calls==1 && last_bound_port==7000);
        assert(server_task_active && server_running && !server_stop_requested);
        assert(rtsp_server_stop_checked()==ESP_OK);
        assert(!server_task_active && !server_running && server_socket==-1 && close_calls==1);
        host_legacy=true; run_after_delays=0;
        assert(rtsp_server_start()==ESP_OK && last_bound_port==5000);
        assert(rtsp_server_stop_checked()==ESP_OK && close_calls==2);
        break;
    case 1: /* Immediate bind failure is not reported as successful task creation. */
        bind_error=1;
        assert(rtsp_server_start()!=ESP_OK);
        assert(bind_calls==1 && listen_calls==0 && close_calls==1);
        assert(!server_task_active && !server_running && server_socket==-1);
        break;
    case 2:
        listen_error=1;
        assert(rtsp_server_start()!=ESP_OK);
        assert(listen_calls==1 && close_calls==1 && !server_task_active);
        break;
    case 3: /* If the task has not run before timeout, it must never bind later. */
        run_after_delays=10000;
        assert(rtsp_server_start()==ESP_ERR_TIMEOUT);
        assert(server_stop_requested && !server_task_active && bind_calls==0);
        assert(socket_calls==0 && close_calls==0);
        break;
    case 4: /* A live canceled acceptor blocks subsequent restart. */
        run_after_delays=10000; allow_task_join=false;
        assert(rtsp_server_start()==ESP_ERR_TIMEOUT);
        assert(server_task_active && server_stop_requested);
        assert(rtsp_server_start()==ESP_ERR_INVALID_STATE && task_creates==1);
        allow_task_join=true;
        assert(rtsp_server_stop_checked()==ESP_OK && !server_task_active);
        break;
    case 5: /* The acceptor exiting is insufficient: both client tasks must join. */
        assert(rtsp_server_start()==ESP_OK);
        clients[0].active=true; clients[0].socket=31; clients[0].conn=(void*)(uintptr_t)0x55;
        clients[1].active=true; clients[1].socket=32; clients[1].conn=(void*)(uintptr_t)0x66;
        client_done_after=4;
        assert(rtsp_server_stop_checked()==ESP_OK);
        assert(delays==4 && !clients[0].active && !clients[1].active && close_calls==3);
        assert(!clients[0].conn && !clients[1].conn);
        break;
    case 6: /* Stop timeout preserves a live slot and its descriptor; no reset/reuse. */
        assert(rtsp_server_start()==ESP_OK);
        clients[0].active=true; clients[0].socket=31; clients[0].conn=(void*)(uintptr_t)0x55;
        assert(rtsp_server_stop_checked()==ESP_ERR_TIMEOUT);
        assert(!server_task_active && clients[0].active && clients[0].should_stop);
        assert(clients[0].socket==31 && clients[0].conn==(void*)(uintptr_t)0x55);
        assert(rtsp_server_start()==ESP_ERR_INVALID_STATE && task_creates==1);
        assert(close_calls==1); /* Only the acceptor's owner closed its socket. */
        client_done_after=delays+1;
        assert(rtsp_server_stop_checked()==ESP_OK && close_calls==2);
        assert(rtsp_server_start()==ESP_OK && task_creates==2);
        assert(rtsp_server_stop_checked()==ESP_OK);
        break;
    case 7:
        task_create_error=1;
        assert(rtsp_server_start()==ESP_FAIL);
        assert(!server_task_active && !server_running && socket_calls==0);
        break;
    case 8:
        fcntl_error=F_SETFL;
        assert(rtsp_server_start()!=ESP_OK);
        assert(close_calls==1 && !server_task_active && !server_running);
        break;
    case 9:
        create_lock_fail_at=1;
        assert(rtsp_server_start()==ESP_ERR_NO_MEM && task_creates==0);
        break;
    default: return 99;
    }
    for(int i=0;i<8;++i) assert(!locks[i].held);
    return 0;
}
'''


class RtspLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="airplay-server-lifecycle-")
        cls.addClassCleanup(cls.temp.cleanup)
        source=(PORT/"rtsp_server.c").read_text(encoding="utf-8")
        declarations=source[source.index("static int server_socket"):
                            source.index("void airplay_set_volume(")]
        bodies="\n\n".join(function(source,name) for name in (
            "static void signal_client_stop(",
            "static bool rtsp_clients_stopped(",
            "static void server_task(",
            "static esp_err_t rtsp_server_stop_locked(",
            "esp_err_t rtsp_server_start(",
            "esp_err_t rtsp_server_stop_checked(",
        ))
        cls.executable,cls.env=build_program(Path(cls.temp.name),"server_lifecycle",
            SERVER_PREFIX+declarations+SERVER_MOCKS+bodies+SERVER_MAIN)

    def run_case(self,number):
        result=subprocess.run([str(self.executable),str(number)],env=self.env,
                              capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_waits_for_readiness_and_rebinds_correct_ports(self): self.run_case(0)
    def test_bind_failure_is_reported_and_socket_closed_once(self): self.run_case(1)
    def test_listen_failure_is_reported_and_joined(self): self.run_case(2)
    def test_timed_out_late_task_never_binds(self): self.run_case(3)
    def test_live_acceptor_blocks_restart_after_timeout(self): self.run_case(4)
    def test_checked_stop_waits_for_both_client_tasks(self): self.run_case(5)
    def test_client_timeout_preserves_slot_and_blocks_restart(self): self.run_case(6)
    def test_task_creation_failure_clears_active_reservation(self): self.run_case(7)
    def test_nonblocking_configuration_failure_cleans_owner(self): self.run_case(8)
    def test_missing_mutex_prevents_task_creation(self): self.run_case(9)


if __name__=="__main__":
    unittest.main(verbosity=2)
