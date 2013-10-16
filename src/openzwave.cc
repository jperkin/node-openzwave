/*
 * Copyright (c) 2013 Jonathan Perkin <jonathan@perkin.org.uk>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <unistd.h>
#include <pthread.h>
#include <list>
#include <queue>

#include <node.h>
#include <v8.h>

#include "Manager.h"
#include "Node.h"
#include "Notification.h"
#include "Options.h"
#include "Value.h"

using namespace v8;
using namespace node;

namespace {

struct OZW: ObjectWrap {
	static Handle<Value> New(const Arguments& args);
	static Handle<Value> Connect(const Arguments& args);
	static Handle<Value> Disconnect(const Arguments& args);
	static Handle<Value> SetLevel(const Arguments& args);
	static Handle<Value> SwitchOn(const Arguments& args);
	static Handle<Value> SwitchOff(const Arguments& args);
};

Persistent<Object> context_obj;

uv_async_t async;

typedef struct {
	uint32_t			m_type;
	uint32_t			m_homeId;
	uint8_t				m_nodeId;
	std::list<OpenZWave::ValueID>	m_values;
} NotifInfo;

typedef struct {
	uint32_t			m_homeId;
	uint8_t				m_nodeId;
	bool				m_polled;
	std::list<OpenZWave::ValueID>	m_values;
} NodeInfo;

/*
 * Message passing queue between OpenZWave callback and v8 async handler.
 */
static pthread_mutex_t zqueue_mutex = PTHREAD_MUTEX_INITIALIZER;
static std::queue<NotifInfo *> zqueue;

/*
 * Node state.
 */
static pthread_mutex_t znodes_mutex = PTHREAD_MUTEX_INITIALIZER;
static std::list<NodeInfo *> znodes;

static uint32_t homeid;

/*
 * OpenZWave callback, just push onto queue and trigger the handler
 * in v8 land.
 */
void cb(OpenZWave::Notification const *cb, void *ctx)
{
	NotifInfo *ni = new NotifInfo();

	ni->m_type = cb->GetType();
	ni->m_homeId = cb->GetHomeId();
	ni->m_nodeId = cb->GetNodeId();
	ni->m_values.push_front(cb->GetValueID());

	pthread_mutex_lock(&zqueue_mutex);
	zqueue.push(ni);
	pthread_mutex_unlock(&zqueue_mutex);

	uv_async_send(&async);
}

/*
 * Async handler, triggered by the OpenZWave callback.
 */
void async_cb_handler(uv_async_t *handle, int status)
{
	NotifInfo *ni;
	Local<Value> args[16];

	pthread_mutex_lock(&zqueue_mutex);

	while (!zqueue.empty())
	{
		ni = zqueue.front();

		switch (ni->m_type) {
		case OpenZWave::Notification::Type_DriverReady:
			homeid = ni->m_homeId;
			args[0] = String::New("driver ready");
			MakeCallback(context_obj, "emit", 1, args);
			break;
		case OpenZWave::Notification::Type_DriverFailed:
			args[0] = String::New("driver failed");
			MakeCallback(context_obj, "emit", 1, args);
			break;
		/*
		 * On NodeNew we can save the information about the new node,
		 * but wait until NodeAdded before announcing it.
		 */
		case OpenZWave::Notification::Type_NodeNew:
		{
			NodeInfo *n = new NodeInfo();
			n->m_homeId = ni->m_homeId;
			n->m_nodeId = ni->m_nodeId;
			n->m_polled = false;
			pthread_mutex_lock(&znodes_mutex);
			znodes.push_back(n);
			pthread_mutex_unlock(&znodes_mutex);
			break;
		}
		case OpenZWave::Notification::Type_NodeAdded:
			args[0] = String::New("node added");
			args[1] = Integer::New(ni->m_nodeId);
			MakeCallback(context_obj, "emit", 2, args);
			break;
		/*
		 * Node values.  For now we only support binary switches and
		 * multi-level devices.
		 */
		case OpenZWave::Notification::Type_ValueAdded:
		case OpenZWave::Notification::Type_ValueChanged:
		{
			OpenZWave::ValueID value = ni->m_values.front();
			const char *evname = (ni->m_type == OpenZWave::Notification::Type_ValueAdded)
			    ? "value added" : "value changed";
			/*
			 * Store the value whether we support it or not.
			 */
			if (ni->m_type == OpenZWave::Notification::Type_ValueAdded) {
				for (std::list<NodeInfo *>::iterator it = znodes.begin(); it != znodes.end(); ++it) {
					NodeInfo *n = *it;
					if (n->m_nodeId == ni->m_nodeId) {
						pthread_mutex_lock(&znodes_mutex);
						n->m_values.push_back(value);
						pthread_mutex_unlock(&znodes_mutex);
						break;
					}
				}
			}
			switch (value.GetCommandClassId()) {
			case 0x25: // COMMAND_CLASS_SWITCH_BINARY
			{
				/*
				 * Binary switches take a bool value for on/off.
				 */
				bool val;
				if (ni->m_type == OpenZWave::Notification::Type_ValueAdded)
					OpenZWave::Manager::Get()->EnablePoll(value, 1);
				OpenZWave::Manager::Get()->GetValueAsBool(value, &val);
				args[0] = String::New(evname);
				args[1] = Integer::New(ni->m_nodeId);
				args[2] = String::New("switch");
				args[3] = Boolean::New(val)->ToBoolean();
				MakeCallback(context_obj, "emit", 4, args);
				break;
			}
			case 0x26: // COMMAND_CLASS_SWITCH_MULTILEVEL
				/*
				 * Multi-level switches take an effective 0-99
				 * range, even though they are 0-255.  255
				 * means "last value if supported, else max".
				 *
				 * This class usually contains a number of
				 * values.  We only care about the first value,
				 * which is the overall level.
				 */
				if (value.GetIndex() == 0) {
					uint8_t val;
					if (ni->m_type == OpenZWave::Notification::Type_ValueAdded)
						OpenZWave::Manager::Get()->EnablePoll(value, 1);
					OpenZWave::Manager::Get()->GetValueAsByte(value, &val);
					args[0] = String::New(evname);
					args[1] = Integer::New(ni->m_nodeId);
					args[2] = String::New("level");
					args[3] = Integer::New(val);
					MakeCallback(context_obj, "emit", 4, args);
				}
				break;
			}
			fprintf(stderr, "unsupported command class: 0x%x\n", value.GetCommandClassId());
			break;
		}
		/*
		 * I believe this means that the node is now ready to accept
		 * commands, however for now we will wait until all queries are
		 * complete before notifying upstack, just in case.
		 */
		case OpenZWave::Notification::Type_EssentialNodeQueriesComplete:
			break;
		/*
		 * The node is now fully ready for operation.
		 */
		case OpenZWave::Notification::Type_NodeQueriesComplete:
		{
			Local<Object> info = Object::New();
			info->Set(String::NewSymbol("manufacturer"),
			    String::New(OpenZWave::Manager::Get()->GetNodeManufacturerName(ni->m_homeId, ni->m_nodeId).c_str()));
			info->Set(String::NewSymbol("product"),
			    String::New(OpenZWave::Manager::Get()->GetNodeProductName(ni->m_homeId, ni->m_nodeId).c_str()));
			info->Set(String::NewSymbol("type"),
			    String::New(OpenZWave::Manager::Get()->GetNodeType(ni->m_homeId, ni->m_nodeId).c_str()));
			info->Set(String::NewSymbol("loc"),
			    String::New(OpenZWave::Manager::Get()->GetNodeLocation(ni->m_homeId, ni->m_nodeId).c_str()));
			args[0] = String::New("node ready");
			args[1] = Integer::New(ni->m_nodeId);
			args[2] = info;
			MakeCallback(context_obj, "emit", 3, args);
			break;
		}
		/*
		 * The network scan has been completed.  Currently we do not
		 * care about dead nodes - is there anything we can do anyway?
		 */
		case OpenZWave::Notification::Type_AllNodesQueried:
		case OpenZWave::Notification::Type_AllNodesQueriedSomeDead:
			args[0] = String::New("scan complete");
			MakeCallback(context_obj, "emit", 1, args);
			break;
		/*
		 * Send unhandled events to stderr so we can monitor them if
		 * necessary.
		 */
		default:
			fprintf(stderr, "Unhandled notification: %d\n", ni->m_type);
			break;
		}

		zqueue.pop();
	}

	pthread_mutex_unlock(&zqueue_mutex);
}

Handle<Value> OZW::New(const Arguments& args)
{
	HandleScope scope;

	assert(args.IsConstructCall());
	OZW* self = new OZW();
	self->Wrap(args.This());

	Local<Object> opts = args[0]->ToObject();

	/*
	 * Options are global for all drivers and can only be set once.
	 */
	OpenZWave::Options::Create("./deps/open-zwave/config", "", "");
	OpenZWave::Options::Get()->AddOptionBool("ConsoleOutput", opts->Get(String::New("consoleoutput"))->BooleanValue());
	OpenZWave::Options::Get()->AddOptionBool("Logging", opts->Get(String::New("logging"))->BooleanValue());
	OpenZWave::Options::Get()->AddOptionBool("SaveConfiguration", opts->Get(String::New("saveconfig"))->BooleanValue());
	OpenZWave::Options::Get()->AddOptionInt("DriverMaxAttempts", opts->Get(String::New("driverattempts"))->IntegerValue());
	OpenZWave::Options::Get()->Lock();

	return scope.Close(args.This());
}

Handle<Value> OZW::Connect(const Arguments& args)
{
	HandleScope scope;

	std::string path = (*String::Utf8Value(args[0]->ToString()));

	uv_async_init(uv_default_loop(), &async, async_cb_handler);

	context_obj = Persistent<Object>::New(args.This());

	OpenZWave::Manager::Create();
	OpenZWave::Manager::Get()->AddWatcher(cb, NULL);
	OpenZWave::Manager::Get()->AddDriver(path);

	Handle<Value> argv[1] = { String::New("connected") };
	MakeCallback(context_obj, "emit", 1, argv);

	return Undefined();
}

Handle<Value> OZW::Disconnect(const Arguments& args)
{
	HandleScope scope;

	std::string path = (*String::Utf8Value(args[0]->ToString()));

	OpenZWave::Manager::Get()->RemoveDriver(path);
	OpenZWave::Manager::Get()->RemoveWatcher(cb, NULL);
	OpenZWave::Manager::Destroy();
	OpenZWave::Options::Destroy();

	return scope.Close(Undefined());
}

/*
 * Switch a COMMAND_CLASS_SWITCH_BINARY on/off
 */
void set_switch(uint8_t node, bool state)
{
	std::list<NodeInfo *>::iterator nit;
	std::list<OpenZWave::ValueID>::iterator vit;

	for (nit = znodes.begin(); nit != znodes.end(); ++nit) {
		NodeInfo *ni = *nit;
		if (ni->m_nodeId == node) {
			for (vit = ni->m_values.begin(); vit != ni->m_values.end(); ++vit) {
				if ((*vit).GetCommandClassId() == 0x25) {
					OpenZWave::Manager::Get()->SetValue(*vit, state);
					break;
				}
			}
		}
	}
}
Handle<Value> OZW::SwitchOn(const Arguments& args)
{
	HandleScope scope;

	uint8_t node = args[0]->ToNumber()->Value();
	set_switch(node, true);

	return scope.Close(Undefined());
}
Handle<Value> OZW::SwitchOff(const Arguments& args)
{
	HandleScope scope;

	uint8_t node = args[0]->ToNumber()->Value();
	set_switch(node, false);

	return scope.Close(Undefined());
}

/*
 * Set a COMMAND_CLASS_SWITCH_MULTILEVEL device to a specific value.
 */
Handle<Value> OZW::SetLevel(const Arguments& args)
{
	HandleScope scope;

	uint8_t node = args[0]->ToNumber()->Value();
	uint8_t value = args[1]->ToNumber()->Value();
	std::list<NodeInfo *>::iterator nit;
	std::list<OpenZWave::ValueID>::iterator vit;

	for (nit = znodes.begin(); nit != znodes.end(); ++nit) {
		NodeInfo *ni = *nit;
		if (ni->m_nodeId == node) {
			for (vit = ni->m_values.begin(); vit != ni->m_values.end(); ++vit) {
				if ((*vit).GetCommandClassId() == 0x26 && (*vit).GetIndex() == 0) {
					OpenZWave::Manager::Get()->SetValue(*vit, value);
					break;
				}
			}
		}
	}

	return scope.Close(Undefined());
}

extern "C" void init(Handle<Object> target)
{
	HandleScope scope;

	Local<FunctionTemplate> t = FunctionTemplate::New(OZW::New);
	t->InstanceTemplate()->SetInternalFieldCount(1);
	t->SetClassName(String::New("OZW"));

	NODE_SET_PROTOTYPE_METHOD(t, "connect", OZW::Connect);
	NODE_SET_PROTOTYPE_METHOD(t, "disconnect", OZW::Disconnect);
	NODE_SET_PROTOTYPE_METHOD(t, "setLevel", OZW::SetLevel);
	NODE_SET_PROTOTYPE_METHOD(t, "switchOn", OZW::SwitchOn);
	NODE_SET_PROTOTYPE_METHOD(t, "switchOff", OZW::SwitchOff);

	target->Set(String::NewSymbol("Emitter"), t->GetFunction());
}

}

NODE_MODULE(openzwave, init)
