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
	static Handle<Value> SetLocation(const Arguments& args);
	static Handle<Value> SetName(const Arguments& args);
	static Handle<Value> SwitchOn(const Arguments& args);
	static Handle<Value> SwitchOff(const Arguments& args);
	static Handle<Value> HardReset(const Arguments& args);
	static Handle<Value> SoftReset(const Arguments& args);
};

Persistent<Object> context_obj;

uv_async_t async;

typedef struct {
	uint32_t			type;
	uint32_t			homeid;
	uint8_t				nodeid;
	uint8_t				groupidx;
	uint8_t				event;
	uint8_t				buttonid;
	uint8_t				sceneid;
	uint8_t				notification;
	std::list<OpenZWave::ValueID>	values;
} NotifInfo;

typedef struct {
	uint32_t			homeid;
	uint8_t				nodeid;
	bool				polled;
	std::list<OpenZWave::ValueID>	values;
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
 * Return the node for this request.
 */
NodeInfo *get_node_info(uint8_t nodeid)
{
	std::list<NodeInfo *>::iterator it;
	NodeInfo *node;

	for (it = znodes.begin(); it != znodes.end(); ++it) {
		node = *it;
		if (node->nodeid == nodeid)
			return node;
	}

	return NULL;
}

/*
 * OpenZWave callback, just push onto queue and trigger the handler
 * in v8 land.
 */
void cb(OpenZWave::Notification const *cb, void *ctx)
{
	NotifInfo *notif = new NotifInfo();

	notif->type = cb->GetType();
	notif->homeid = cb->GetHomeId();
	notif->nodeid = cb->GetNodeId();
	notif->values.push_front(cb->GetValueID());

	/*
	 * Some values are only set on particular notifications, and
	 * assertions in openzwave prevent us from trying to fetch them
	 * unconditionally.
	 */
	switch (notif->type) {
	case OpenZWave::Notification::Type_Group:
		notif->groupidx = cb->GetGroupIdx();
		break;
	case OpenZWave::Notification::Type_NodeEvent:
		notif->event = cb->GetEvent();
		break;
	case OpenZWave::Notification::Type_CreateButton:
	case OpenZWave::Notification::Type_DeleteButton:
	case OpenZWave::Notification::Type_ButtonOn:
	case OpenZWave::Notification::Type_ButtonOff:
		notif->buttonid = cb->GetButtonId();
		break;
	case OpenZWave::Notification::Type_SceneEvent:
		notif->sceneid = cb->GetSceneId();
		break;
	case OpenZWave::Notification::Type_Notification:
		notif->notification = cb->GetNotification();
		break;
	}

	pthread_mutex_lock(&zqueue_mutex);
	zqueue.push(notif);
	pthread_mutex_unlock(&zqueue_mutex);

	uv_async_send(&async);
}

/*
 * Async handler, triggered by the OpenZWave callback.
 */
void async_cb_handler(uv_async_t *handle, int status)
{
	NodeInfo *node;
	NotifInfo *notif;
	Local<Value> args[16];

	pthread_mutex_lock(&zqueue_mutex);

	while (!zqueue.empty())
	{
		notif = zqueue.front();

		switch (notif->type) {
		case OpenZWave::Notification::Type_DriverReady:
			homeid = notif->homeid;
			args[0] = String::New("driver ready");
			args[1] = Integer::New(homeid);
			MakeCallback(context_obj, "emit", 2, args);
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
			node = new NodeInfo();
			node->homeid = notif->homeid;
			node->nodeid = notif->nodeid;
			node->polled = false;
			pthread_mutex_lock(&znodes_mutex);
			znodes.push_back(node);
			pthread_mutex_unlock(&znodes_mutex);
			break;
		}
		case OpenZWave::Notification::Type_NodeAdded:
			args[0] = String::New("node added");
			args[1] = Integer::New(notif->nodeid);
			MakeCallback(context_obj, "emit", 2, args);
			break;
		/*
		 * Ignore intermediate notifications about a node status, we
		 * wait until the node is ready before retrieving information.
		 */
		case OpenZWave::Notification::Type_NodeProtocolInfo:
		case OpenZWave::Notification::Type_NodeNaming:
		case OpenZWave::Notification::Type_PollingEnabled:
			break;
		/*
		 * Node values.  For now we only support binary switches and
		 * multi-level devices.
		 */
		case OpenZWave::Notification::Type_ValueAdded:
		case OpenZWave::Notification::Type_ValueChanged:
		{
			OpenZWave::ValueID value = notif->values.front();
			const char *evname = (notif->type == OpenZWave::Notification::Type_ValueAdded)
			    ? "value added" : "value changed";
			/*
			 * Store the value whether we support it or not.
			 */
			if (notif->type == OpenZWave::Notification::Type_ValueAdded) {
				if ((node = get_node_info(notif->nodeid))) {
					pthread_mutex_lock(&znodes_mutex);
					node->values.push_back(value);
					pthread_mutex_unlock(&znodes_mutex);
				}
			}
			switch (value.GetCommandClassId()) {
			case 0x25: // COMMAND_CLASS_SWITCH_BINARY
			{
				/*
				 * Binary switches take a bool value for on/off.
				 */
				bool val;
				Local<Object> valobj = Object::New();

				if (notif->type == OpenZWave::Notification::Type_ValueAdded) {
					OpenZWave::Manager::Get()->EnablePoll(value, 1);
					OpenZWave::Manager::Get()->SetChangeVerified(value, true);
				}

				valobj->Set(String::NewSymbol("genre"),
					String::New(OpenZWave::Value::GetGenreNameFromEnum(value.GetGenre())));
				valobj->Set(String::NewSymbol("instance"),
					Integer::New(value.GetInstance()));
				valobj->Set(String::NewSymbol("index"),
					Integer::New(value.GetIndex()));
				valobj->Set(String::NewSymbol("label"),
					String::New(OpenZWave::Manager::Get()->GetValueLabel(value).c_str()));
				OpenZWave::Manager::Get()->GetValueAsBool(value, &val);
				valobj->Set(String::NewSymbol("value"), Boolean::New(val)->ToBoolean());

				args[0] = String::New(evname);
				args[1] = Integer::New(notif->nodeid);
				args[2] = Integer::New(value.GetCommandClassId());
				args[3] = valobj;
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
					Local<Object> valobj = Object::New();

					if (notif->type == OpenZWave::Notification::Type_ValueAdded) {
						OpenZWave::Manager::Get()->EnablePoll(value, 1);
						OpenZWave::Manager::Get()->SetChangeVerified(value, true);
					}

					valobj->Set(String::NewSymbol("genre"),
						String::New(OpenZWave::Value::GetGenreNameFromEnum(value.GetGenre())));
					valobj->Set(String::NewSymbol("instance"),
						Integer::New(value.GetInstance()));
					valobj->Set(String::NewSymbol("index"),
						Integer::New(value.GetIndex()));
					valobj->Set(String::NewSymbol("label"),
						String::New(OpenZWave::Manager::Get()->GetValueLabel(value).c_str()));
					OpenZWave::Manager::Get()->GetValueAsByte(value, &val);
					valobj->Set(String::NewSymbol("value"), Integer::New(val));
					OpenZWave::Manager::Get()->GetValueAsByte(value, &val);

					args[0] = String::New(evname);
					args[1] = Integer::New(notif->nodeid);
					args[2] = Integer::New(value.GetCommandClassId());
					args[3] = valobj;
					MakeCallback(context_obj, "emit", 4, args);
				}
				break;
			case 0x86: // COMMAND_CLASS_VERSION
			{
				std::string cur;
				Local<Object> val = Object::New();

				val->Set(String::NewSymbol("genre"),
					String::New(OpenZWave::Value::GetGenreNameFromEnum(value.GetGenre())));
				val->Set(String::NewSymbol("instance"),
					Integer::New(value.GetInstance()));
				val->Set(String::NewSymbol("index"),
					Integer::New(value.GetIndex()));
				val->Set(String::NewSymbol("label"),
					String::New(OpenZWave::Manager::Get()->GetValueLabel(value).c_str()));
				OpenZWave::Manager::Get()->GetValueAsString(value, &cur);
				val->Set(String::NewSymbol("value"), String::New(cur.c_str()));

				args[0] = String::New(evname);
				args[1] = Integer::New(notif->nodeid);
				args[2] = Integer::New(value.GetCommandClassId());
				args[3] = val;
				MakeCallback(context_obj, "emit", 4, args);
				break;
			}
			default:
				fprintf(stderr, "unsupported command class: 0x%x\n", value.GetCommandClassId());
				break;
			}
			break;
		}
		/*
		 * A value update was sent but nothing changed, likely due to
		 * the value just being polled.  Ignore, as we handle actual
		 * changes above.
		 */
		case OpenZWave::Notification::Type_ValueRefreshed:
			break;
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
			    String::New(OpenZWave::Manager::Get()->GetNodeManufacturerName(notif->homeid, notif->nodeid).c_str()));
			info->Set(String::NewSymbol("product"),
			    String::New(OpenZWave::Manager::Get()->GetNodeProductName(notif->homeid, notif->nodeid).c_str()));
			info->Set(String::NewSymbol("type"),
			    String::New(OpenZWave::Manager::Get()->GetNodeType(notif->homeid, notif->nodeid).c_str()));
			info->Set(String::NewSymbol("name"),
			    String::New(OpenZWave::Manager::Get()->GetNodeName(notif->homeid, notif->nodeid).c_str()));
			info->Set(String::NewSymbol("loc"),
			    String::New(OpenZWave::Manager::Get()->GetNodeLocation(notif->homeid, notif->nodeid).c_str()));
			args[0] = String::New("node ready");
			args[1] = Integer::New(notif->nodeid);
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
			fprintf(stderr, "Unhandled notification: %d\n", notif->type);
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
	std::string confpath = (*String::Utf8Value(opts->Get(String::New("modpath")->ToString())));
	confpath += "/../deps/open-zwave/config";

	/*
	 * Options are global for all drivers and can only be set once.
	 */
	OpenZWave::Options::Create(confpath.c_str(), "", "");
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
 * Set a COMMAND_CLASS_SWITCH_MULTILEVEL device to a specific value.
 */
Handle<Value> OZW::SetLevel(const Arguments& args)
{
	HandleScope scope;

	uint8_t nodeid = args[0]->ToNumber()->Value();
	uint8_t value = args[1]->ToNumber()->Value();
	NodeInfo *node;
	std::list<OpenZWave::ValueID>::iterator vit;

	if ((node = get_node_info(nodeid))) {
		for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
			if ((*vit).GetCommandClassId() == 0x26 && (*vit).GetIndex() == 0) {
				OpenZWave::Manager::Get()->SetValue(*vit, value);
				break;
			}
		}
	}

	return scope.Close(Undefined());
}

/*
 * Write a new location string to the device, if supported.
 */
Handle<Value> OZW::SetLocation(const Arguments& args)
{
	HandleScope scope;

	uint8_t nodeid = args[0]->ToNumber()->Value();
	std::string location = (*String::Utf8Value(args[1]->ToString()));

	OpenZWave::Manager::Get()->SetNodeLocation(homeid, nodeid, location);

	return scope.Close(Undefined());
}

/*
 * Write a new name string to the device, if supported.
 */
Handle<Value> OZW::SetName(const Arguments& args)
{
	HandleScope scope;

	uint8_t nodeid = args[0]->ToNumber()->Value();
	std::string name = (*String::Utf8Value(args[1]->ToString()));

	OpenZWave::Manager::Get()->SetNodeName(homeid, nodeid, name);

	return scope.Close(Undefined());
}

/*
 * Switch a COMMAND_CLASS_SWITCH_BINARY on/off
 */
void set_switch(uint8_t nodeid, bool state)
{
	NodeInfo *node;
	std::list<OpenZWave::ValueID>::iterator vit;

	if ((node = get_node_info(nodeid))) {
		for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
			if ((*vit).GetCommandClassId() == 0x25) {
				OpenZWave::Manager::Get()->SetValue(*vit, state);
				break;
			}
		}
	}
}
Handle<Value> OZW::SwitchOn(const Arguments& args)
{
	HandleScope scope;

	uint8_t nodeid = args[0]->ToNumber()->Value();
	set_switch(nodeid, true);

	return scope.Close(Undefined());
}
Handle<Value> OZW::SwitchOff(const Arguments& args)
{
	HandleScope scope;

	uint8_t nodeid = args[0]->ToNumber()->Value();
	set_switch(nodeid, false);

	return scope.Close(Undefined());
}

/*
 * Reset the ZWave controller chip.  A hard reset is destructive and wipes
 * out all known configuration, a soft reset just restarts the chip.
 */
Handle<Value> OZW::HardReset(const Arguments& args)
{
	HandleScope scope;

	OpenZWave::Manager::Get()->ResetController(homeid);

	return scope.Close(Undefined());
}
Handle<Value> OZW::SoftReset(const Arguments& args)
{
	HandleScope scope;

	OpenZWave::Manager::Get()->SoftReset(homeid);

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
	NODE_SET_PROTOTYPE_METHOD(t, "setLocation", OZW::SetLocation);
	NODE_SET_PROTOTYPE_METHOD(t, "setName", OZW::SetName);
	NODE_SET_PROTOTYPE_METHOD(t, "switchOn", OZW::SwitchOn);
	NODE_SET_PROTOTYPE_METHOD(t, "switchOff", OZW::SwitchOff);
	NODE_SET_PROTOTYPE_METHOD(t, "hardReset", OZW::HardReset);
	NODE_SET_PROTOTYPE_METHOD(t, "softReset", OZW::SoftReset);

	target->Set(String::NewSymbol("Emitter"), t->GetFunction());
}

}

NODE_MODULE(openzwave, init)
