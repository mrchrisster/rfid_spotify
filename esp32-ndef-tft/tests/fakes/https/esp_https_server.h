#pragma once
#include <map>
#include <string>
#include <vector>
#include <deque>
#include <cstring>
#include <stdint.h>
using esp_err_t=int;
constexpr int ESP_OK=0, HTTP_GET=0, HTTP_POST=1, pdTRUE=1;
constexpr int HTTPD_500_INTERNAL_SERVER_ERROR=500, HTTPD_400_BAD_REQUEST=400, HTTPD_403_FORBIDDEN=403;
struct FakeQueue { size_t size; size_t capacity; std::deque<std::vector<uint8_t>> entries; };
using QueueHandle_t=FakeQueue*;
inline QueueHandle_t xQueueCreate(size_t n,size_t size) { return new FakeQueue{size,n,{}}; }
inline int xQueueSend(QueueHandle_t q,const void* p,int) {
 if(q->entries.size()>=q->capacity)return 0;
 auto* b=static_cast<const uint8_t*>(p);q->entries.emplace_back(b,b+q->size);return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t q,void* p,int) {
 if(q->entries.empty())return 0;
 memcpy(p,q->entries.front().data(),q->size);q->entries.pop_front();return pdTRUE;
}
struct httpd_req_t {
 size_t content_len=0; std::string body,query,status="200 OK",output;
 std::map<std::string,std::string> headers,responseHeaders;
 size_t read=0;
};
inline int copyTo(const std::string& s,char* out,size_t n) { if(s.size()>=n)return -1;memcpy(out,s.c_str(),s.size()+1);return ESP_OK; }
inline int httpd_req_get_hdr_value_str(httpd_req_t* r,const char* key,char* out,size_t n) {
 auto it=r->headers.find(key);return it==r->headers.end()?-1:copyTo(it->second,out,n);
}
inline void httpd_resp_set_hdr(httpd_req_t* r,const char* k,const char* v){r->responseHeaders[k]=v;}
inline void httpd_resp_set_status(httpd_req_t* r,const char* s){r->status=s;}
inline void httpd_resp_set_type(httpd_req_t* r,const char* s){r->responseHeaders["Content-Type"]=s;}
inline int httpd_resp_sendstr(httpd_req_t* r,const char* s){r->output=s;return ESP_OK;}
inline int httpd_resp_send_err(httpd_req_t* r,int code,const char* s){r->status=std::to_string(code);r->output=s;return ESP_OK;}
inline int httpd_req_recv(httpd_req_t* r,char* out,size_t size){size=std::min(size,std::min(size_t(11),r->body.size()-r->read));memcpy(out,r->body.data()+r->read,size);r->read+=size;return size;}
inline size_t httpd_req_get_url_query_len(httpd_req_t* r){return r->query.size();}
inline int httpd_req_get_url_query_str(httpd_req_t* r,char* out,size_t n){return copyTo(r->query,out,n);}
struct FakeConfig {int task_priority,stack_size,max_open_sockets,max_uri_handlers;size_t max_uri_len,max_req_hdr_len;bool lru_purge_enable;int recv_wait_timeout,send_wait_timeout;};
struct httpd_ssl_config_t {FakeConfig httpd;const uint8_t* servercert;size_t servercert_len;const uint8_t* prvtkey_pem;size_t prvtkey_len;int tls_handshake_timeout_ms;};
#define HTTPD_SSL_CONFIG_DEFAULT() {}
using httpd_handle_t=void*;
struct httpd_uri_t {const char* uri;int method;int(*handler)(httpd_req_t*);};
inline int httpd_ssl_start(httpd_handle_t* s,httpd_ssl_config_t*){*s=reinterpret_cast<void*>(1);return ESP_OK;}
inline bool fakeStopFailure=false;
inline int httpd_ssl_stop(httpd_handle_t){return fakeStopFailure ? -1 : ESP_OK;}
inline int httpd_register_uri_handler(httpd_handle_t,httpd_uri_t*){return ESP_OK;}
