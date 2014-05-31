#include <node.h>
#include <v8.h>
#include <dns_sd.h>
#include <mutex>
#include <map>
#include <functional>
#include <pthread.h>

using namespace v8;
using namespace node;
using namespace std;

Persistent<Function> AdvertisementConstructor;
Persistent<Function> BrowserConstructor;
Persistent<Function> Require;
map<DNSServiceRef volatile *, Local<Object> > refMap;

void NewAdvertisement(const FunctionCallbackInfo<Value> &args) {
	auto isolate = Isolate::GetCurrent();

	if (args.IsConstructCall()) {
		auto volatile ref = new DNSServiceRef;
		DNSServiceRegister(ref, 0, 0, NULL, *String::Utf8Value(args[0]->ToString()), NULL, NULL, args[1]->Int32Value(), 0, NULL, NULL, NULL);
		args.This()->SetAlignedPointerInInternalField(args.This()->InternalFieldCount() - 1, ref);
		args.This()->Set(String::NewSymbol("service"), args[0]->ToString(), ReadOnly);
		args.This()->Set(String::NewSymbol("port"), Number::New(args[1]->Int32Value()), ReadOnly);
		args.This()->Set(String::NewSymbol("advertising"), True(isolate), ReadOnly);
		args.GetReturnValue().Set(args.This());

	} else {
		auto cons = Local<Function>::New(isolate, AdvertisementConstructor);
		args.GetReturnValue().Set(cons->NewInstance(2, (Local<Value>[]){ args[0], args[1] }));
	}
}

void NewBrowser(const FunctionCallbackInfo<Value> &args) {
	auto isolate = Isolate::GetCurrent();

	if (args.IsConstructCall()) {		
		auto volatile ref = new DNSServiceRef;
		auto require = Local<Function>::New(isolate, Require);
		auto Socket = Local<Function>::Cast(Local<Object>::Cast(require->Call(Null(isolate), 1, (Local<Value>[]){ String::NewFromUtf8(isolate, "net") }))->Get(String::NewSymbol("Socket")));
		
		DNSServiceBrowse(ref, 0, 0, *String::Utf8Value(args[0]->ToString()), NULL, [](DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName, const char *regtype, const char *replyDomain, void *context) {
			/*
			auto isolate = Isolate::GetCurrent();

			auto tpl = FunctionTemplate::New();
			tpl->SetClassName(String::NewFromUtf8(isolate, "DNSServiceBrowserReply"));
			tpl->InstanceTemplate()->SetInternalFieldCount(0);

			auto ret = tpl->InstanceTemplate()->NewInstance();
			auto type = "";

			if (flags & kDNSServiceFlagsAdd)
				type = "found";
			else
				type = "lost";

			ret->Set(String::NewSymbol("type"), String::NewFromUtf8(isolate, type));
			ret->Set(String::NewSymbol("host"), String::NewFromUtf8(isolate, serviceName));
			ret->Set(String::NewSymbol("service"), String::NewFromUtf8(isolate, regtype));
			MakeCallback(refMap[&sdRef], "emit", 2, (Local<Value>[]){ String::NewFromUtf8(isolate, type), ret });
			*/
			//(*(function<void()> *)refMap[&sdRef]->GetAlignedPointerFromInternalField(refMap[&sdRef]->InternalFieldCount() - 2))();
		}, NULL);
		

		auto opts = Object::New();
		opts->Set(String::NewSymbol("fd"), Integer::New(DNSServiceRefSockFD(*ref)));
		auto socket = Socket->NewInstance(1, (Local<Value>[]){ opts });
		socket->SetHiddenValue(String::NewSymbol("browser"), args.This());

		Local<Function>::Cast(socket->Get(String::NewSymbol("once")))->Call(socket, 2, (Local<Value>[]){ String::NewFromUtf8(isolate, "data"), FunctionTemplate::New([](const FunctionCallbackInfo<Value> &args) {
			auto isolate = Isolate::GetCurrent();
			auto ref = (DNSServiceRef *)Local<Object>::Cast(args.Holder()->GetHiddenValue(String::NewSymbol("browser")))->GetAlignedPointerFromInternalField(args.Holder()->InternalFieldCount() - 1);

			if (ref)
				DNSServiceProcessResult(*ref);
			else
				ThrowException(Exception::ReferenceError(String::NewFromUtf8(isolate, "Invoked processResult on terminated browser")));
		})->GetFunction() });

		args.This()->SetAlignedPointerInInternalField(args.This()->InternalFieldCount() - 1, ref);
		args.This()->Set(String::NewSymbol("service"), args[0]->ToString(), ReadOnly);
		args.This()->Set(String::NewSymbol("listening"), True(isolate), ReadOnly);

		refMap[ref] = Local<Object>::New(isolate, args.This());
		args.GetReturnValue().Set(args.This());

	} else {
		auto cons = Local<Function>::New(isolate, BrowserConstructor);
		args.GetReturnValue().Set(cons->NewInstance(1, (Local<Value>[]){ args[0] }));
	}}

void Terminate(const FunctionCallbackInfo<Value> &args) {
	auto isolate = Isolate::GetCurrent();
	auto ref = (DNSServiceRef *)args.Holder()->GetAlignedPointerFromInternalField(args.Holder()->InternalFieldCount() - 1);

	if (ref) {
		assert(ref != NULL);
		DNSServiceRefDeallocate(*ref);
		delete ref;
		args.Holder()->SetAlignedPointerInInternalField(args.Holder()->InternalFieldCount() - 1, NULL);

		if (args.Holder()->GetConstructorName()->Equals(String::NewFromUtf8(isolate, "DNSServiceAdvertisement")))
			args.Holder()->ForceSet(String::NewSymbol("advertising"), False(isolate), ReadOnly);
		else if (args.Holder()->GetConstructorName()->Equals(String::NewFromUtf8(isolate, "DNSServiceBrowser")))
			args.Holder()->ForceSet(String::NewSymbol("listening"), False(isolate), ReadOnly);
		else
			ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Terminate called on object of unknown type")));
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
	// Get handle to require
	NODE_SET_METHOD(exports, "registerRequire", [](const FunctionCallbackInfo<Value> &args) {
		auto isolate = Isolate::GetCurrent();

		Require.Reset(isolate, Local<Function>::Cast(args[0]));
		args.Holder()->ForceDelete(String::NewSymbol("registerRequire"));
		args.GetReturnValue().Set(args.Holder());

		// Initializing Advertisement
		{
			auto tpl = FunctionTemplate::New(NewAdvertisement);
			
			tpl->SetClassName(String::NewFromUtf8(isolate, "DNSServiceAdvertisement"));
			tpl->InstanceTemplate()->SetInternalFieldCount(1);
			NODE_SET_PROTOTYPE_METHOD(tpl, "terminate", Terminate);

			AdvertisementConstructor.Reset(isolate, tpl->GetFunction());
			NODE_SET_METHOD(args.Holder(), "DNSServiceAdvertisement", NewAdvertisement);
		}

		// Initializing Browser
		{
			auto tpl = FunctionTemplate::New(NewBrowser);
			auto require = Local<Function>::New(isolate, Require);
			auto inherits = Local<Function>::Cast(Local<Object>::Cast(require->Call(Null(isolate), 1, (Local<Value>[]){ String::NewFromUtf8(isolate, "util") }))->Get(String::NewSymbol("inherits")));
			auto EventEmitter = Local<Function>::Cast(Local<Object>::Cast(require->Call(Null(isolate), 1, (Local<Value>[]){ String::NewFromUtf8(isolate, "events") }))->Get(String::NewSymbol("EventEmitter")));

			auto tempEventEmitter = EventEmitter->NewInstance();
			
			tpl->SetClassName(String::NewFromUtf8(isolate, "DNSServiceBrowser"));
			tpl->InstanceTemplate()->SetInternalFieldCount(tempEventEmitter->InternalFieldCount() + 2);
			NODE_SET_PROTOTYPE_METHOD(tpl, "terminate", Terminate);

			auto ConstructorFunc = tpl->GetFunction();

			inherits->Call(Null(isolate), 2, (Local<Value>[]){ ConstructorFunc, EventEmitter });
			
			BrowserConstructor.Reset(isolate, ConstructorFunc);
			NODE_SET_METHOD(args.Holder(), "DNSServiceBrowser", NewBrowser);
		}
	});
}

NODE_MODULE(dns_sd, init)