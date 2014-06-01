#include <node.h>
#include <v8.h>
#include <dns_sd.h>
#include <map>
#include <pthread.h>
#include <uv.h>

using namespace v8;
using namespace node;
using namespace std;

Persistent<Function> AdvertisementConstructor;
Persistent<Function> BrowserConstructor;
Persistent<Function> Require;
map<DNSServiceRef, Persistent<Object> *> refMap;
map<uv_stream_t *, DNSServiceRef> fdSocketHandleMap;

void NewAdvertisement(const FunctionCallbackInfo<Value> &args) {
	auto isolate = Isolate::GetCurrent();

	if (args.IsConstructCall()) {
		DNSServiceRef ref;
		if (DNSServiceRegister(&ref, 0, 0, NULL, *String::Utf8Value(args[0]->ToString()), NULL, NULL, args[1]->Int32Value(), 0, NULL, NULL, NULL) == kDNSServiceErr_NoError) {
			args.This()->SetAlignedPointerInInternalField(args.This()->InternalFieldCount() - 1, ref);
			args.This()->Set(String::NewSymbol("service"), args[0]->ToString(), ReadOnly);
			args.This()->Set(String::NewSymbol("port"), Number::New(args[1]->Int32Value()), ReadOnly);
			args.This()->Set(String::NewSymbol("advertising"), True(isolate), ReadOnly);
			args.GetReturnValue().Set(args.This());
		} else
			ThrowException(Exception::Error(String::NewFromUtf8(isolate, "DNSServiceRegister returned an error code")));

	} else
		args.GetReturnValue().Set(Local<Function>::New(isolate, AdvertisementConstructor)->NewInstance(2, (Local<Value>[]){ args[0], args[1] }));
}

void NewBrowser(const FunctionCallbackInfo<Value> &args) {
	auto isolate = Isolate::GetCurrent();

	if (args.IsConstructCall()) {		
		DNSServiceRef ref;
		
		if (DNSServiceBrowse(&ref, 0, 0, *String::Utf8Value(args[0]->ToString()), NULL, [](DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName, const char *regtype, const char *replyDomain, void *context) {
			
			if (errorCode == kDNSServiceErr_NoError) {
			
				DNSServiceRef resolveRef;
				auto fdSocketHandle = new uv_pipe_t;
				
				if (DNSServiceResolve(&resolveRef, flags, interfaceIndex, serviceName, regtype, replyDomain, [](DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *fullname, const char *hosttarget, uint16_t port,  uint16_t txtLen, const unsigned char *txtRecord, void *context) {
					auto isolate = Isolate::GetCurrent();
					HandleScope scope(isolate);
					
					auto This = Local<Object>::New(isolate, *refMap[(DNSServiceRef)((void **)context)[2]]);
					
					if (errorCode == kDNSServiceErr_NoError) {
						
						auto tpl = FunctionTemplate::New();
						tpl->SetClassName(String::NewFromUtf8(isolate, "DNSServiceBrowserReply"));
						tpl->InstanceTemplate()->SetInternalFieldCount(0);
						
						auto ret = tpl->InstanceTemplate()->NewInstance();
						auto type = "";
						
						if (*(DNSServiceFlags *)((void **)context)[1] & kDNSServiceFlagsAdd)
							type = "found";
						else
							type = "lost";
						
						ret->Set(String::NewSymbol("fullName"), String::NewFromUtf8(isolate, fullname));
						ret->Set(String::NewSymbol("hostTarget"), String::NewFromUtf8(isolate, hosttarget));
						ret->Set(String::NewSymbol("port"), Integer::New(port));
						MakeCallback(This, "emit", 2, (Local<Value>[]){ String::NewFromUtf8(isolate, type), ret });
						
					} else
						
						MakeCallback(This, "emit", 2, (Local<Value>[]){ String::NewFromUtf8(isolate, "error"), Exception::Error(String::NewFromUtf8(isolate, "DNSServiceResolveReply returned an error code")) });
					
					uv_read_stop((uv_stream_t *)((void **)context)[0]);
					fdSocketHandleMap.erase((uv_stream_t *)((void **)context)[0]);
					delete (uv_stream_t *)((void **)context)[0];
					delete (DNSServiceFlags *)((void **)context)[1];
					delete (void **)context;
					
				}, new (void *[3]){ fdSocketHandle, new DNSServiceFlags(flags), sdRef }) == kDNSServiceErr_NoError) {
					
					uv_pipe_init(uv_default_loop(), fdSocketHandle, 0);
					int sockFd = DNSServiceRefSockFD(resolveRef);
					
					if (sockFd >= 0) {
					
						uv_pipe_open(fdSocketHandle, sockFd);
						uv_read_start((uv_stream_t *)fdSocketHandle, [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
							buf->len = 0;
						}, [](uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
							DNSServiceProcessResult(fdSocketHandleMap[stream]);
						});
						fdSocketHandleMap[(uv_stream_t *)fdSocketHandle] = resolveRef;
					
					} else {
						
						auto isolate = Isolate::GetCurrent();
						HandleScope scope(isolate);
						
						auto This = Local<Object>::New(isolate, *refMap[sdRef]);
						MakeCallback(This, "emit", 2, (Local<Value>[]){ String::NewFromUtf8(isolate, "error"), Exception::Error(String::NewFromUtf8(isolate, "DNSServiceRefSockFD returned an error code")) });
						
					}
				
				} else {
					
					auto isolate = Isolate::GetCurrent();
					HandleScope scope(isolate);
					
					auto This = Local<Object>::New(isolate, *refMap[sdRef]);
					MakeCallback(This, "emit", 2, (Local<Value>[]){ String::NewFromUtf8(isolate, "error"), Exception::Error(String::NewFromUtf8(isolate, "DNSServiceResolve returned an error code")) });
				}
					
			} else {
				
				auto isolate = Isolate::GetCurrent();
				HandleScope scope(isolate);
				
				auto This = Local<Object>::New(isolate, *refMap[sdRef]);
				MakeCallback(This, "emit", 2, (Local<Value>[]){ String::NewFromUtf8(isolate, "error"), Exception::Error(String::NewFromUtf8(isolate, "DNSServiceBrowseReply returned an error code")) });
			}
				
		}, NULL) == kDNSServiceErr_NoError) {
			
			auto fdSocketHandle = new uv_pipe_t;
			uv_pipe_init(uv_default_loop(), fdSocketHandle, 0);
			int sockFd = DNSServiceRefSockFD(ref);
			
			if (sockFd >= 0) {
			
				uv_pipe_open(fdSocketHandle, DNSServiceRefSockFD(ref));
				uv_read_start((uv_stream_t *)fdSocketHandle, [](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
					buf->len = 0;
				}, [](uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
					DNSServiceProcessResult(fdSocketHandleMap[stream]);
				});
				
				args.This()->SetAlignedPointerInInternalField(args.This()->InternalFieldCount() - 1, ref);
				args.This()->SetAlignedPointerInInternalField(args.This()->InternalFieldCount() - 2, fdSocketHandle);
				args.This()->Set(String::NewSymbol("service"), args[0]->ToString(), ReadOnly);
				args.This()->Set(String::NewSymbol("listening"), True(isolate), ReadOnly);
				
				auto persistentThis = new Persistent<Object>;
				persistentThis->Reset(isolate, args.This());
				refMap[ref] = persistentThis;
				fdSocketHandleMap[(uv_stream_t *)fdSocketHandle] = ref;
				args.GetReturnValue().Set(args.This());
				
			} else
				
				MakeCallback(args.This(), "emit", 2, (Local<Value>[]){ String::NewFromUtf8(isolate, "error"), Exception::Error(String::NewFromUtf8(isolate, "DNSServiceRefSockFD returned an error code")) });
				
		} else
			
			ThrowException(Exception::Error(String::NewFromUtf8(isolate, "DNSServiceBrowse returned an error code")));

	} else
		
		args.GetReturnValue().Set(Local<Function>::New(isolate, BrowserConstructor)->NewInstance(1, (Local<Value>[]){ args[0] }));
}

void Terminate(const FunctionCallbackInfo<Value> &args) {
	auto isolate = Isolate::GetCurrent();
	auto ref = (DNSServiceRef)args.Holder()->GetAlignedPointerFromInternalField(args.Holder()->InternalFieldCount() - 1);

	if (ref) {
		if (args.Holder()->GetConstructorName()->Equals(String::NewFromUtf8(isolate, "DNSServiceAdvertisement")))
			args.Holder()->ForceSet(String::NewSymbol("advertising"), False(isolate), ReadOnly);
		else if (args.Holder()->GetConstructorName()->Equals(String::NewFromUtf8(isolate, "DNSServiceBrowser"))) {
			auto fdSocketHandle = (uv_stream_t *)args.Holder()->GetAlignedPointerFromInternalField(args.Holder()->InternalFieldCount() - 2);
			args.Holder()->ForceSet(String::NewSymbol("listening"), False(isolate), ReadOnly);
			uv_read_stop(fdSocketHandle);
			fdSocketHandleMap.erase(fdSocketHandle);
			delete fdSocketHandle;
			refMap[ref]->Reset();
			delete refMap[ref];
			refMap.erase(ref);
		}
		else
			ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Terminate called on object of unknown type")));
		
		DNSServiceRefDeallocate(ref);
		args.Holder()->SetAlignedPointerInInternalField(args.Holder()->InternalFieldCount() - 1, NULL);
		args.GetReturnValue().Set(args.Holder());

	} else {
		if (args.Holder()->GetConstructorName()->Equals(String::NewFromUtf8(isolate, "DNSServiceAdvertisement")))
			ThrowException(Exception::ReferenceError(String::NewFromUtf8(isolate, "Advertisement already terminated")));
		else if (args.Holder()->GetConstructorName()->Equals(String::NewFromUtf8(isolate, "DNSServiceBrowser")))
			ThrowException(Exception::ReferenceError(String::NewFromUtf8(isolate, "Browser already terminated")));
		else
			ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Terminate called on object of unknown type with no internal ref")));
	}
}

void init(Handle<Object> exports) {
	auto isolate = Isolate::GetCurrent();
	
	Require.Reset(isolate, Local<Function>::Cast(Script::Compile(String::NewFromUtf8(isolate, "require"))->Run()));
	
	// Initializing Advertisement
	{
		auto tpl = FunctionTemplate::New(NewAdvertisement);
		
		tpl->SetClassName(String::NewFromUtf8(isolate, "DNSServiceAdvertisement"));
		tpl->InstanceTemplate()->SetInternalFieldCount(1);
		NODE_SET_PROTOTYPE_METHOD(tpl, "terminate", Terminate);
		
		AdvertisementConstructor.Reset(isolate, tpl->GetFunction());
		NODE_SET_METHOD(exports, "DNSServiceAdvertisement", NewAdvertisement);
	}
	
	// Initializing Browser
	{
		auto tpl = FunctionTemplate::New(NewBrowser);
		auto require = Local<Function>::New(isolate, Require);
		auto inherits = Local<Function>::Cast(Local<Object>::Cast(require->Call(Null(isolate), 1, (Local<Value>[]){ String::NewFromUtf8(isolate, "util") }))->Get(String::NewSymbol("inherits")));
		auto EventEmitter = Local<Function>::Cast(Local<Object>::Cast(require->Call(Null(isolate), 1, (Local<Value>[]){ String::NewFromUtf8(isolate, "events") }))->Get(String::NewSymbol("EventEmitter")));
		
		tpl->SetClassName(String::NewFromUtf8(isolate, "DNSServiceBrowser"));
		
		auto tempEventEmitter = EventEmitter->NewInstance();
		tpl->InstanceTemplate()->SetInternalFieldCount(tempEventEmitter->InternalFieldCount() + 2);
		
		auto ConstructorFunc = tpl->GetFunction();
		inherits->Call(Null(isolate), 2, (Local<Value>[]){ ConstructorFunc, EventEmitter });
		Local<Object>::Cast(ConstructorFunc->Get(String::NewSymbol("prototype")))->Set(String::NewSymbol("terminate"), FunctionTemplate::New(Terminate)->GetFunction());
		
		BrowserConstructor.Reset(isolate, ConstructorFunc);
		NODE_SET_METHOD(exports, "DNSServiceBrowser", NewBrowser);
	}
}

NODE_MODULE(dns_sd, init)