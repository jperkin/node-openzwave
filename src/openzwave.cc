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

#include <nan.h>
#include <v8.h>

#include "Manager.h"
#include "Node.h"
#include "Notification.h"
#include "Options.h"
#include "Value.h"

using namespace v8;
using namespace node;

namespace {

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

class OZW : public Nan::ObjectWrap {
 public:
	OZW() {
		async_.data = this;
	}

	static NAN_MODULE_INIT(Init);
	static NAN_METHOD(New);
	static NAN_METHOD(Connect);
	static NAN_METHOD(Disconnect);
	static NAN_METHOD(SetValue);
	static NAN_METHOD(SetLevel);
	static NAN_METHOD(SetLocation);
	static NAN_METHOD(SetName);
	static NAN_METHOD(SwitchOn);
	static NAN_METHOD(SwitchOff);
	static NAN_METHOD(EnablePoll);
	static NAN_METHOD(DisablePoll);
	static NAN_METHOD(HardReset);
	static NAN_METHOD(SoftReset);

 private:
	static void onNotify(OpenZWave::Notification const *cb, void *ctx);
	static void asyncHandler(uv_async_t *handle);

	NodeInfo* getNodeInfo(uint8_t nodeid);
	void setSwitch(uint8_t nodeid, bool state);

 private:
	Nan::Callback emit_;
	uv_async_t async_;
	uint32_t homeid_;

	/*
	 * Message passing queue between OpenZWave callback and v8 async handler.
	 */
	pthread_mutex_t zqueue_mutex_ = PTHREAD_MUTEX_INITIALIZER;
	std::queue<NotifInfo *> zqueue_;
	
	/*
	 * Message passing queue between OpenZWave callback and v8 async handler.
	 */
	pthread_mutex_t znodes_mutex_ = PTHREAD_MUTEX_INITIALIZER;
	std::list<NodeInfo *> znodes_;
};

/*
 * Return the node for this request.
 */
NodeInfo* OZW::getNodeInfo(uint8_t nodeid)
{
	std::list<NodeInfo *>::iterator it;
	NodeInfo *node;

	for (it = znodes_.begin(); it != znodes_.end(); ++it) {
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
void OZW::onNotify(OpenZWave::Notification const *cb, void *ctx)
{
	OZW* self = (OZW*) ctx;
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

	pthread_mutex_lock(&self->zqueue_mutex_);
	self->zqueue_.push(notif);
	pthread_mutex_unlock(&self->zqueue_mutex_);

	uv_async_send(&self->async_);
}

/*
 * Async handler, triggered by the OpenZWave callback.
 */
void OZW::asyncHandler(uv_async_t *handle)//, int status)
{
	Nan::HandleScope scope;
	OZW* self = (OZW*) handle->data;
	NodeInfo *node;
	NotifInfo *notif;
	Local<Value> args[16];

	pthread_mutex_lock(&self->zqueue_mutex_);

	while (!self->zqueue_.empty())
	{
		notif = self->zqueue_.front();

		switch (notif->type) {
		case OpenZWave::Notification::Type_DriverReady:
			self->homeid_ = notif->homeid;
			args[0] = Nan::New("driver ready").ToLocalChecked();
			args[1] = Nan::New(self->homeid_);
			self->emit_.Call(2, args);
			break;
		case OpenZWave::Notification::Type_DriverFailed:
			args[0] = Nan::New("driver failed").ToLocalChecked();
			self->emit_.Call(1, args);
			break;
		/*
		 * NodeNew is triggered when a node is discovered which is not
		 * found in the OpenZWave XML file.  As we do not use that file
		 * simply ignore those notifications for now.
		 *
		 * NodeAdded is when we actually have a new node to set up.
		 */
		case OpenZWave::Notification::Type_NodeNew:
			break;
		case OpenZWave::Notification::Type_NodeAdded:
			node = new NodeInfo();
			node->homeid = notif->homeid;
			node->nodeid = notif->nodeid;
			node->polled = false;
			pthread_mutex_lock(&self->znodes_mutex_);
			self->znodes_.push_back(node);
			pthread_mutex_unlock(&self->znodes_mutex_);
			args[0] = Nan::New("node added").ToLocalChecked();
			args[1] = Nan::New(notif->nodeid);
			self->emit_.Call(2, args);
			break;
		/*
		 * Ignore intermediate notifications about a node status, we
		 * wait until the node is ready before retrieving information.
		 */
		case OpenZWave::Notification::Type_NodeProtocolInfo:
		case OpenZWave::Notification::Type_NodeNaming:
		// XXX: these should be supported correctly.
		case OpenZWave::Notification::Type_PollingEnabled:
		case OpenZWave::Notification::Type_PollingDisabled:
			break;
		/*
		 * Node values.
		 */
		case OpenZWave::Notification::Type_ValueAdded:
		case OpenZWave::Notification::Type_ValueChanged:
		{
			OpenZWave::ValueID value = notif->values.front();
			Local<Object> valobj = Nan::New<Object>();
			const char *evname = (notif->type == OpenZWave::Notification::Type_ValueAdded)
					? "value added" : "value changed";

			if (notif->type == OpenZWave::Notification::Type_ValueAdded) {
				if ((node = self->getNodeInfo(notif->nodeid))) {
					pthread_mutex_lock(&self->znodes_mutex_);
					node->values.push_back(value);
					pthread_mutex_unlock(&self->znodes_mutex_);
				}
				OpenZWave::Manager::Get()->SetChangeVerified(value, true);
			}

			/*
			 * Common value types.
			 */
			valobj->Set(Nan::New("type").ToLocalChecked(),
						Nan::New(OpenZWave::Value::GetTypeNameFromEnum(value.GetType())).ToLocalChecked());
			valobj->Set(Nan::New("genre").ToLocalChecked(),
						Nan::New(OpenZWave::Value::GetGenreNameFromEnum(value.GetGenre())).ToLocalChecked());
			valobj->Set(Nan::New("instance").ToLocalChecked(),
						Nan::New(value.GetInstance()));
			valobj->Set(Nan::New("index").ToLocalChecked(),
						Nan::New(value.GetIndex()));
			valobj->Set(Nan::New("label").ToLocalChecked(),
						Nan::New(OpenZWave::Manager::Get()->GetValueLabel(value).c_str()).ToLocalChecked());
			valobj->Set(Nan::New("units").ToLocalChecked(),
						Nan::New(OpenZWave::Manager::Get()->GetValueUnits(value).c_str()).ToLocalChecked());
			valobj->Set(Nan::New("read_only").ToLocalChecked(),
						Nan::New(OpenZWave::Manager::Get()->IsValueReadOnly(value))->ToBoolean());
			valobj->Set(Nan::New("write_only").ToLocalChecked(),
						Nan::New(OpenZWave::Manager::Get()->IsValueWriteOnly(value))->ToBoolean());
			// XXX: verify_changes=
			// XXX: poll_intensity=
			valobj->Set(Nan::New("min").ToLocalChecked(),
						Nan::New(OpenZWave::Manager::Get()->GetValueMin(value)));
			valobj->Set(Nan::New("max").ToLocalChecked(),
						Nan::New(OpenZWave::Manager::Get()->GetValueMax(value)));

			/*
			 * The value itself is type-specific.
			 */
			switch (value.GetType()) {
			case OpenZWave::ValueID::ValueType_Bool:
			{
				bool val;
				OpenZWave::Manager::Get()->GetValueAsBool(value, &val);
				valobj->Set(Nan::New("value").ToLocalChecked(), Nan::New(val));
				break;
			}
			case OpenZWave::ValueID::ValueType_Byte:
			{
				uint8_t val;
				OpenZWave::Manager::Get()->GetValueAsByte(value, &val);
				valobj->Set(Nan::New("value").ToLocalChecked(), Nan::New(val));
				break;
			}
			case OpenZWave::ValueID::ValueType_Decimal:
			{
				float val;
				OpenZWave::Manager::Get()->GetValueAsFloat(value, &val);
				valobj->Set(Nan::New("value").ToLocalChecked(), Nan::New(val));
				break;
			}
			case OpenZWave::ValueID::ValueType_Int:
			{
				int32_t val;
				OpenZWave::Manager::Get()->GetValueAsInt(value, &val);
				valobj->Set(Nan::New("value").ToLocalChecked(), Nan::New(val));
				break;
			}
			case OpenZWave::ValueID::ValueType_List:
			{
				Local<Array> items;
			}
			case OpenZWave::ValueID::ValueType_Short:
			{
				int16_t val;
				OpenZWave::Manager::Get()->GetValueAsShort(value, &val);
				valobj->Set(Nan::New("value").ToLocalChecked(), Nan::New(val));
				break;
			}
			case OpenZWave::ValueID::ValueType_String:
			{
				std::string val;
				OpenZWave::Manager::Get()->GetValueAsString(value, &val);
				valobj->Set(Nan::New("value").ToLocalChecked(), Nan::New(val).ToLocalChecked());
				break;
			}
			/*
			 * Buttons do not have a value.
			 */
			case OpenZWave::ValueID::ValueType_Button:
			{
				break;
			}
			default:
				fprintf(stderr, "unsupported value type: 0x%x\n", value.GetType());
				break;
			}

			args[0] = Nan::New(evname).ToLocalChecked();
			args[1] = Nan::New(notif->nodeid);
			args[2] = Nan::New(value.GetCommandClassId());
			args[3] = valobj;
			self->emit_.Call(4, args);

			break;
		}
		/*
		 * A value update was sent but nothing changed, likely due to
		 * the value just being polled.  Ignore, as we handle actual
		 * changes above.
		 */
		case OpenZWave::Notification::Type_ValueRefreshed:
			break;
		case OpenZWave::Notification::Type_ValueRemoved:
		{
			OpenZWave::ValueID value = notif->values.front();
			std::list<OpenZWave::ValueID>::iterator vit;
			if ((node = self->getNodeInfo(notif->nodeid))) {
				for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
					if ((*vit) == notif->values.front()) {
						node->values.erase(vit);
						break;
					}
				}
			}
			args[0] = Nan::New("value removed").ToLocalChecked();
			args[1] = Nan::New(notif->nodeid);
			args[2] = Nan::New(value.GetCommandClassId());
			args[3] = Nan::New(value.GetIndex());
			self->emit_.Call(4, args);
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
			Local<Object> info = Nan::New<Object>();
			info->Set(Nan::New("manufacturer").ToLocalChecked(),
					Nan::New(OpenZWave::Manager::Get()->GetNodeManufacturerName(notif->homeid, notif->nodeid).c_str()).ToLocalChecked());
			info->Set(Nan::New("manufacturerid").ToLocalChecked(),
					Nan::New(OpenZWave::Manager::Get()->GetNodeManufacturerId(notif->homeid, notif->nodeid).c_str()).ToLocalChecked());
			info->Set(Nan::New("product").ToLocalChecked(),
					Nan::New(OpenZWave::Manager::Get()->GetNodeProductName(notif->homeid, notif->nodeid).c_str()).ToLocalChecked());
			info->Set(Nan::New("producttype").ToLocalChecked(),
					Nan::New(OpenZWave::Manager::Get()->GetNodeProductType(notif->homeid, notif->nodeid).c_str()).ToLocalChecked());
			info->Set(Nan::New("productid").ToLocalChecked(),
					Nan::New(OpenZWave::Manager::Get()->GetNodeProductId(notif->homeid, notif->nodeid).c_str()).ToLocalChecked());
			info->Set(Nan::New("type").ToLocalChecked(),
					Nan::New(OpenZWave::Manager::Get()->GetNodeType(notif->homeid, notif->nodeid).c_str()).ToLocalChecked());
			info->Set(Nan::New("name").ToLocalChecked(),
					Nan::New(OpenZWave::Manager::Get()->GetNodeName(notif->homeid, notif->nodeid).c_str()).ToLocalChecked());
			info->Set(Nan::New("loc").ToLocalChecked(),
					Nan::New(OpenZWave::Manager::Get()->GetNodeLocation(notif->homeid, notif->nodeid).c_str()).ToLocalChecked());
			args[0] = Nan::New("node ready").ToLocalChecked();
			args[1] = Nan::New(notif->nodeid);
			args[2] = info;
			self->emit_.Call(3, args);
			break;
		}
		/*
		 * The network scan has been completed.  Currently we do not
		 * care about dead nodes - is there anything we can do anyway?
		 */
		case OpenZWave::Notification::Type_AwakeNodesQueried:
		case OpenZWave::Notification::Type_AllNodesQueried:
		case OpenZWave::Notification::Type_AllNodesQueriedSomeDead:
			args[0] = Nan::New("scan complete").ToLocalChecked();
			self->emit_.Call(1, args);
			break;
		/*
		 * A general notification.
		 */
		case OpenZWave::Notification::Type_Notification:
			args[0] = Nan::New("notification").ToLocalChecked();
			args[1] = Nan::New(notif->nodeid);
			args[2] = Nan::New(notif->notification);
			self->emit_.Call(3, args);
			break;
		/*
		 * Send unhandled events to stderr so we can monitor them if
		 * necessary.
		 */
		default:
			fprintf(stderr, "Unhandled notification: %d\n", notif->type);
			break;
		}

		self->zqueue_.pop();
	}

	pthread_mutex_unlock(&self->zqueue_mutex_);
}

NAN_METHOD(OZW::New)
{
	assert(info.IsConstructCall());
	OZW* self = new OZW();
	self->Wrap(info.This());

	Local<Object> opts = info[0]->ToObject();
	std::string confpath = (*String::Utf8Value(opts->Get(Nan::New("modpath").ToLocalChecked()->ToString())));
	confpath += "/../deps/open-zwave/config";

	/*
	 * Options are global for all drivers and can only be set once.
	 */
	OpenZWave::Options::Create(confpath.c_str(), "", "");
	OpenZWave::Options::Get()->AddOptionBool("ConsoleOutput",
			opts->Get(Nan::New("consoleoutput").ToLocalChecked())->BooleanValue());
	OpenZWave::Options::Get()->AddOptionBool("Logging",
			opts->Get(Nan::New("logging").ToLocalChecked())->BooleanValue());
	OpenZWave::Options::Get()->AddOptionBool("SaveConfiguration",
			opts->Get(Nan::New("saveconfig").ToLocalChecked())->BooleanValue());
	OpenZWave::Options::Get()->AddOptionInt("DriverMaxAttempts",
			opts->Get(Nan::New("driverattempts").ToLocalChecked())->IntegerValue());
	OpenZWave::Options::Get()->AddOptionInt("PollInterval",
			opts->Get(Nan::New("pollinterval").ToLocalChecked())->IntegerValue());
	OpenZWave::Options::Get()->AddOptionBool("IntervalBetweenPolls", true);
	OpenZWave::Options::Get()->AddOptionBool("SuppressValueRefresh",
			opts->Get(Nan::New("suppressrefresh").ToLocalChecked())->BooleanValue());
	std::string networkKey = (*String::Utf8Value(opts->Get(Nan::New("networkkey").ToLocalChecked()->ToString())));	
	OpenZWave::Options::Get()->AddOptionString("NetworkKey", networkKey, false);
	OpenZWave::Options::Get()->Lock();

	self->emit_.SetFunction(info[1].As<Function>());
	info.GetReturnValue().Set(info.This());
}


NAN_METHOD(OZW::Connect)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	std::string path = (*String::Utf8Value(info[0]->ToString()));

	uv_async_init(uv_default_loop(), &self->async_, &OZW::asyncHandler);

	OpenZWave::Manager::Create();
	OpenZWave::Manager::Get()->AddWatcher(&OZW::onNotify, self);
	OpenZWave::Manager::Get()->AddDriver(path);

	Local<Value> argv[1] = { Nan::New("connected").ToLocalChecked() };
	self->emit_.Call(1, argv);
}

NAN_METHOD(OZW::Disconnect)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	std::string path = (*String::Utf8Value(info[0]->ToString()));

	OpenZWave::Manager::Get()->RemoveDriver(path);
	OpenZWave::Manager::Get()->RemoveWatcher(&OZW::onNotify, self);
	OpenZWave::Manager::Destroy();
	OpenZWave::Options::Destroy();
}

/*
 * Generic value set.
 */
NAN_METHOD(OZW::SetValue)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	uint8_t nodeid = info[0]->ToNumber()->Value();
	uint8_t comclass = info[1]->ToNumber()->Value();
	uint8_t index = info[2]->ToNumber()->Value();

	NodeInfo *node;
	std::list<OpenZWave::ValueID>::iterator vit;

	if ((node = self->getNodeInfo(nodeid))) {
		for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
			if (((*vit).GetCommandClassId() == comclass) &&
					((*vit).GetIndex() == index)) {

				switch ((*vit).GetType()) {
				case OpenZWave::ValueID::ValueType_Bool:
				{
					bool val = info[3]->ToBoolean()->Value();
					OpenZWave::Manager::Get()->SetValue(*vit, val);
					break;
				}
				case OpenZWave::ValueID::ValueType_Byte:
				{
					uint8_t val = info[3]->ToInteger()->Value();
					OpenZWave::Manager::Get()->SetValue(*vit, val);
					break;
				}
				case OpenZWave::ValueID::ValueType_Decimal:
				{
					float val = info[3]->ToNumber()->NumberValue();
					OpenZWave::Manager::Get()->SetValue(*vit, val);
					break;
				}
				case OpenZWave::ValueID::ValueType_Int:
				{
					int32_t val = info[3]->ToInteger()->Value();
					OpenZWave::Manager::Get()->SetValue(*vit, val);
					break;
				}
				case OpenZWave::ValueID::ValueType_Short:
				{
					int16_t val = info[3]->ToInteger()->Value();
					OpenZWave::Manager::Get()->SetValue(*vit, val);
					break;
				}
				case OpenZWave::ValueID::ValueType_String:
				{
					std::string val = (*String::Utf8Value(info[3]->ToString()));
					OpenZWave::Manager::Get()->SetValue(*vit, val);
					break;
				}
				}
			}
		}
	}
}

/*
 * Set a COMMAND_CLASS_SWITCH_MULTILEVEL device to a specific value.
 */
NAN_METHOD(OZW::SetLevel)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	uint8_t nodeid = info[0]->ToNumber()->Value();
	uint8_t value = info[1]->ToNumber()->Value();

	NodeInfo *node;
	std::list<OpenZWave::ValueID>::iterator vit;

	if ((node = self->getNodeInfo(nodeid))) {
		for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
			if ((*vit).GetCommandClassId() == 0x26 && (*vit).GetIndex() == 0) {
				OpenZWave::Manager::Get()->SetValue(*vit, value);
				break;
			}
		}
	}
}

/*
 * Write a new location string to the device, if supported.
 */
NAN_METHOD(OZW::SetLocation)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	uint8_t nodeid = info[0]->ToNumber()->Value();
	std::string location = (*String::Utf8Value(info[1]->ToString()));

	OpenZWave::Manager::Get()->SetNodeLocation(self->homeid_, nodeid, location);
}

/*
 * Write a new name string to the device, if supported.
 */
NAN_METHOD(OZW::SetName)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	uint8_t nodeid = info[0]->ToNumber()->Value();
	std::string name = (*String::Utf8Value(info[1]->ToString()));

	OpenZWave::Manager::Get()->SetNodeName(self->homeid_, nodeid, name);
}

/*
 * Switch a COMMAND_CLASS_SWITCH_BINARY on/off
 */
void OZW::setSwitch(uint8_t nodeid, bool state)
{
	NodeInfo *node;
	std::list<OpenZWave::ValueID>::iterator vit;

	if ((node = getNodeInfo(nodeid))) {
		for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
			if ((*vit).GetCommandClassId() == 0x25) {
				OpenZWave::Manager::Get()->SetValue(*vit, state);
				break;
			}
		}
	}
}

NAN_METHOD(OZW::SwitchOn)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	uint8_t nodeid = info[0]->ToNumber()->Value();
	self->setSwitch(nodeid, true);
}

NAN_METHOD(OZW::SwitchOff)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	uint8_t nodeid = info[0]->ToNumber()->Value();
	self->setSwitch(nodeid, false);
}

/*
 * Enable/Disable polling on a COMMAND_CLASS basis.
 */
NAN_METHOD(OZW::EnablePoll)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	uint8_t nodeid = info[0]->ToNumber()->Value();
	uint8_t comclass = info[1]->ToNumber()->Value();
	NodeInfo *node;
	std::list<OpenZWave::ValueID>::iterator vit;

	if ((node = self->getNodeInfo(nodeid))) {
		for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
			if ((*vit).GetCommandClassId() == comclass) {
				OpenZWave::Manager::Get()->EnablePoll((*vit), 1);
				break;
			}
		}
	}
}

NAN_METHOD(OZW::DisablePoll)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	uint8_t nodeid = info[0]->ToNumber()->Value();
	uint8_t comclass = info[1]->ToNumber()->Value();
	NodeInfo *node;
	std::list<OpenZWave::ValueID>::iterator vit;

	if ((node = self->getNodeInfo(nodeid))) {
		for (vit = node->values.begin(); vit != node->values.end(); ++vit) {
			if ((*vit).GetCommandClassId() == comclass) {
				OpenZWave::Manager::Get()->DisablePoll((*vit));
				break;
			}
		}
	}
}

/*
 * Reset the ZWave controller chip.  A hard reset is destructive and wipes
 * out all known configuration, a soft reset just restarts the chip.
 */
NAN_METHOD(OZW::HardReset)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	OpenZWave::Manager::Get()->ResetController(self->homeid_);
}

NAN_METHOD(OZW::SoftReset)
{
	OZW* self = ObjectWrap::Unwrap<OZW>(info.This());
	OpenZWave::Manager::Get()->SoftReset(self->homeid_);
}


NAN_MODULE_INIT(OZW::Init) {
	Local<FunctionTemplate> t = Nan::New<FunctionTemplate>(New);
	t->InstanceTemplate()->SetInternalFieldCount(1);
	t->SetClassName(Nan::New("OZW").ToLocalChecked());

	Nan::SetPrototypeMethod(t, "connect", Connect);
/*
	NODE_SET_PROTOTYPE_METHOD(t, "connect", OZW::Connect);
	NODE_SET_PROTOTYPE_METHOD(t, "disconnect", OZW::Disconnect);
	NODE_SET_PROTOTYPE_METHOD(t, "setValue", OZW::SetValue);
	NODE_SET_PROTOTYPE_METHOD(t, "setLevel", OZW::SetLevel);
	NODE_SET_PROTOTYPE_METHOD(t, "setLocation", OZW::SetLocation);
	NODE_SET_PROTOTYPE_METHOD(t, "setName", OZW::SetName);
	NODE_SET_PROTOTYPE_METHOD(t, "switchOn", OZW::SwitchOn);
	NODE_SET_PROTOTYPE_METHOD(t, "switchOff", OZW::SwitchOff);
	NODE_SET_PROTOTYPE_METHOD(t, "enablePoll", OZW::EnablePoll);
	NODE_SET_PROTOTYPE_METHOD(t, "disablePoll", OZW::EnablePoll);
	NODE_SET_PROTOTYPE_METHOD(t, "hardReset", OZW::HardReset);
	NODE_SET_PROTOTYPE_METHOD(t, "softReset", OZW::SoftReset);
*/
	target->Set(Nan::New("Emitter").ToLocalChecked(), t->GetFunction());
}

}

NODE_MODULE(openzwave, OZW::Init)
