#include <node.h>
#include <v8.h>
#include <dns_sd.h>
#include <mutex>
#include <map>

using namespace v8;
using namespace node;
using namespace std;

Persistent<Function> AdvertisementConstructor;
Persistent<Function> BrowserConstructor;
map<DNSServiceRef volatile *, Local<Object> > refMap;

void NewAdvertisement(const FunctionCallbackInfo<Value> &args) {
	auto isolate = Isolate::GetCurrent();

	if (args.IsConstructCall()) {
		auto volatile ref = new DNSServiceRef;
		DNSServiceRegister(ref, 0, 0, NULL, *String::Utf8Value(args[0]->ToString()), NULL, NULL, args[1]->Int32Value(), 0, NULL, NULL, NULL);
		args.This()->SetAlignedPointerInInternalField(0, ref);
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
		
		DNSServiceBrowse(ref, 0, 0, *String::Utf8Value(args[0]->ToString()), NULL, [](DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName, const char *regtype, const char *replyDomain, void *context) {
			auto isolate = Isolate::GetCurrent();
			ThrowException(String::NewFromUtf8(isolate, "Got something"));

			//Local<Value> argv[1] = { True(isolate) };
			//MakeCallback(refMap[&sdRef], "emit", 1, argv);
		}, NULL);

		args.This()->SetAlignedPointerInInternalField(0, ref);
		args.This()->Set(String::NewSymbol("service"), args[0]->ToString(), ReadOnly);
		args.This()->Set(String::NewSymbol("listening"), True(isolate), ReadOnly);
		args.This()->Set(String::NewSymbol("sockFd"), Integer::New(DNSServiceRefSockFD(*ref)), ReadOnly);
		
		NODE_SET_METHOD(args.This(), "processResult", [](const FunctionCallbackInfo<Value> &args){
			auto isolate = Isolate::GetCurrent();
			auto ref = (DNSServiceRef *)args.Holder()->GetAlignedPointerFromInternalField(0);
			if (ref) {
				DNSServiceProcessResult(*ref);
				args.GetReturnValue().Set(args.Holder());
			} else {
				ThrowException(Exception::ReferenceError(String::NewFromUtf8(isolate, "Invoked processResult on terminated browser")));
			}
		});
		
		NODE_SET_METHOD(args.This(), "removeInits", [](const FunctionCallbackInfo<Value> &args){
			auto isolate = Isolate::GetCurrent();
			if (args.Holder()->Has(String::NewSymbol("removeInits"))) {
				args.Holder()->ForceDelete(String::NewSymbol("sockFd"));
				args.Holder()->ForceDelete(String::NewSymbol("processResult"));
				args.Holder()->ForceDelete(String::NewSymbol("removeInits"));
				args.GetReturnValue().Set(args.Holder());
			} else {
				ThrowException(Exception::ReferenceError(String::NewFromUtf8(isolate, "Inits already removed")));
			}
		});

		refMap[ref] = Local<Object>::New(isolate, args.This());
		args.GetReturnValue().Set(args.This());

	} else {
		auto cons = Local<Function>::New(isolate, BrowserConstructor);
		args.GetReturnValue().Set(cons->NewInstance(2, (Local<Value>[]){ args[0], args[1] }));
	}}

void Terminate(const FunctionCallbackInfo<Value> &args) {
	auto isolate = Isolate::GetCurrent();
	auto ref = (DNSServiceRef *)args.Holder()->GetAlignedPointerFromInternalField(0);

	if (ref) {
		assert(ref != NULL);
		DNSServiceRefDeallocate(*ref);
		delete ref;
		args.Holder()->SetAlignedPointerInInternalField(0, NULL);
		args.Holder()->SetAlignedPointerInInternalField(1, NULL);

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
	auto isolate = Isolate::GetCurrent();

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
		
		tpl->SetClassName(String::NewFromUtf8(isolate, "DNSServiceBrowser"));
		tpl->InstanceTemplate()->SetInternalFieldCount(1);
		NODE_SET_PROTOTYPE_METHOD(tpl, "terminate", Terminate);
		
		BrowserConstructor.Reset(isolate, tpl->GetFunction());
		NODE_SET_METHOD(exports, "DNSServiceBrowser", NewBrowser);
	}
}

NODE_MODULE(dns_sd, init)